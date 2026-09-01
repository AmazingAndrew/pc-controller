<p align="right">
  <strong>简体中文</strong> · <a href="ble-pitfalls.md">English</a>
</p>

# BLE / HID 踩坑记录

本页记录把 BLE HID 演示遥控器落到 FoloToy AI Passport 上踩到的真实
坑点。现象描述以当前 PC Controller 代码为参照；根因分析引用
`main/pc_storage.h`、`main/pc_ble_hid.c` 与受管的 NimBLE host
（`components/esp_ble_hid`）。

本页是 fork 内部笔记，不替代上游对 NimBLE Security Manager 的要求
（见 `docs/software-design/pc-controller/requirements.md`）。

## 1. 槽位记录丢了地址类型

### 现象
与某台主机完成配对并重启后，设备进入定向广播并使用
`BLE_ADDR_PUBLIC`，但有些对端（尤其是会重新随机化身份地址的
macOS 笔记本）用随机地址广播。结果首次定向广播窗口对不上，
悄悄降级为通用广播 + 2 分钟窗口，丢掉了"定向 30 秒"这条快速
回连路径。

### 根因
`pc_slot_t` 只持久化对端 6 字节地址（`addr[6]`）。配对事件回调里
抓到的地址类型（`BLE_ADDR_TYPE_PUBLIC` 与 `BLE_ADDR_TYPE_RANDOM`
及其隐私变体）在写入 NVS 之前被丢弃，因此重启后定向广播默认就是
`BLE_ADDR_PUBLIC`。

### 修复
扩展 `pc_slot_t` 增加一个 `addr_type` 字节（缺失时回填
`BLE_ADDR_PUBLIC`，保持与旧版槽位 NVS 的兼容性），把它贯通到
`pc_slot_load` / `pc_slot_save`，再传入
`pc_ble_hid_start_adv_directed()`，让定向广播使用真实记录的地址
类型。

### 经验
- 配对回调带回的身份属性必须整组落盘；只保存看得见的 6 字节
  地址不够。
- 只要 NimBLE 的接口签名里出现 `addr_type`，它的源头就必须是可信
  的（通常是最初的 GAP 事件），不能用一个常量拼。

## 2. `pc_slot_clear` 没有解绑

### 现象
在菜单（条目 2 = "Slot Clear"）清除槽位后，被删除的槽位下一次
主机启动时还会作为"曾经绑定"出现。主机可能用缓存的长期密钥自动
重连，把设备从待机里拽出来——而用户并没按任何键。

### 根因
`pc_slot_clear()` 只擦掉了该槽命名空间下的 NVS 键。NimBLE 自己另
外维护一份 bond 数据库（通常在它自己的 `ble_hs` / `ble_store`
flash 区），宿主的长期密钥、对端 IRK / LTK 以及本地的 CSRK 在
"清除槽位"之后都还在 flash 里。

### 修复
增强 `pc_slot_clear()`：先把对端地址传入 `ble_gap_unpair()`（或
`nimble_port_clear_bond_db()`）解绑，再 / 后清 NVS。即使解绑失败
也要降级打日志并继续清 NVS，确保 UI 不会卡在一个已经不存在的槽
位上。

### 经验
- "清除槽位"是一次比"忘掉元数据"更重的操作，必须同时切断
  NimBLE 与应用存储并行的信任关系。
- 即便菜单上的清除反馈是无声的，解绑失败也要落到日志里，不能
  装作没事。

## 3. 释放报告丢失导致卡键

### 现象
长时间使用 PowerPoint 翻页时，偶尔出现主机把某个键视为持续按
下——设备不停发同一动作的 press（例如 Page Down），却没有对应的
release。主机进入自动重复，或在全屏模式下幻灯片一路翻过去。

### 根因
`pc_ble_hid_send_keyboard()` 同步发出 press 报告，并依赖同一条
调用链路再发一次"清零"的 release 报告。如果第二次 NimBLE 发送
失败（例如刚发完 press 时瞬时射频争用），设备在逻辑上把这次
press 一直挂着，没有针对 release 做补偿 / 重试。

### 修复
在 release 路径上加一次重试：第二次 NimBLE 发送失败时，用同一
`(mods, key)` 在下一次应用任务循环里重新尝试一次 release。再加
一条"强制清零"兜底定时器（例如 500 ms）：任何 press 超期未 release
就发一份全零报告兜底。

### 经验
- HID 键盘报告是两帧的：press + release。release 帧要与 press
  帧同样可靠；press 可以"尽力而为"，release 必须保证。
- 周期性看门狗 / 兜底在卡键场景中是合理的廉价方案：每窗口多
  发一份空报告的开销，远低于一次失控演示带来的复盘成本。
