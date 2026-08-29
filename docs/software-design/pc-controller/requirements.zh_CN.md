<p align="right">
  <strong>简体中文</strong> · <a href="requirements.md">English</a>
</p>

# PC Controller —— 需求规格说明书（Requirements Specification）

> 适用范围：AI PASSPORT 开发板（ESP32-C3）。
> Baseline: dev-plan v0.1 (2026-08-29) + confirmed product decisions (2026-08-30)。

本文档是 “PC Controller” 应用的需求规格说明书：一款运行在现有 AI PASSPORT 硬件上、不改板也不动分区的 BLE HID 演示遥控器。下文所述已确认的产品基线是唯一事实源；凡与固件开发方案（dev-plan v0.1，2026-08-29，维护于本仓库之外，故仅以文字引述）不一致之处，均以已确认基线为准。

## 1. 概述与目标（Overview and Goals）

PC Controller 将 AI PASSPORT 开发板变为蓝牙演示遥控器：幻灯片翻页、全屏控制、一键锁屏主机、媒体控制与演讲计时器，整体界面为全屏 FUI 风格，由三个按键驱动。

核心功能基线：

- BLE HID 走 NimBLE HOGP：HID Service `0x1812` + Device Information `0x180A` + Battery Service `0x180F`；双报告——标准 6KRO 键盘报告与 Consumer Page 报告。
- 翻页：`UP` 下一页 / `DOWN` 上一页。
- 全屏切换：`OK` 短按交替发送 `F5` 与 `Esc`。
- 一键锁屏：仅待机模式可直接触发（媒体模式先经 `OK` 长按返回待机后再触发）；`OK` 长按 ≥800 ms；按主机档案发送组合键：Windows = `Win + L`、macOS = `Ctrl + Cmd + Q`、Linux = `Super + L`。
- 媒体控制：Consumer Page 的音量加减与播放/暂停。
- 演讲计时器：每次进入演示模式清零。
- 三设备槽位串行切换（受 `MAX_CONNECTIONS=1` 约束）。
- 配对/重配对：菜单入口、覆盖绑定、清空槽位，首次开机无绑定时自动进入配对；用户指南小节须提醒用户重配对前先在主机侧“删除设备”。
- 按键音（全局可静音，默认关闭）。
- 背光/睡眠策略：15 秒无操作降档、60 秒熄屏，随后轻睡眠直至深睡。

与默认演示固件的关系声明：PC Controller 是**独立构建档应用；默认演示固件保持不变**（independent build profile application; default demo firmware unchanged）。两个构建档共用同一套 `main/` 源码、BSP、分区表与仓库工具链；默认档构建产物保持字节级不变。

### 排除项（Non-Goals）

- 黑屏功能取消（dev-plan 中双击触发 `B` 键的方案作废）。
- `OK` 双击留空：`BSP_BTN_DOUBLE` 事件保留在事件词汇表中，但任何模式下都不绑定动作。
- 演示模式禁用锁屏与音量；演示模式下 `OK` 长按 = 返回待机。
- 页码为**本地估算**：进入全屏从 1 开始计数，翻页 ±1；不显示总页数、不加 “EST” 标注。
- 屏幕文字全英文；本版本不引入中文字库。

## 2. 范围与适用性（Scope and Applicability）

- 交付形式：独立构建档，通过 `SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.presenter.defaults"` 选择。
- presenter 档与默认档的差异仅在 sdkconfig：关闭 Wi-Fi（`CONFIG_ESP_WIFI_ENABLED=n`）、开启 NimBLE SM、`CONFIG_BT_NIMBLE_NVS_PERSIST=y`（当前在 [sdkconfig.defaults](../../../sdkconfig.defaults) 中为 `n`）；保持 `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`。
- [partitions.csv](../../../partitions.csv) 的分区契约不动：factory 应用 ≤3 MB、`cardid` @ `0x356000`、`recovery` @ `0x700000`。
- ESP-IDF 严格保持 5.5.3。
- GPIO0 三重身份契约不改（三键共用 ADC 采样脚、开机按住 5 秒进 Recovery、ESP32-C3 boot strap）；长按语义仅运行期注册并生效。开机按住 `UP` 进入 ROM 下载模式仍是文档明示的工厂行为。

## 3. 硬件与平台约束（Hardware and Platform Constraints）

硬件事实维护在硬件文档中，本文不复制硬件数据：

- [AI 硬件开发指南](../../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md)
- [硬件规格](../../hardware-design/specifications.zh_CN.md)
- [bsp_pins.h](../../../components/bsp/include/bsp_pins.h)——引脚与硬件参数的唯一事实源。

影响本设计的软件侧约束：

- LVGL 仅使用 240×20 行单缓冲（`double_buffer=false`），禁止加大。
- 屏幕无 TE 信号：大面积重绘存在撕裂窗口，动画必须控制在局部小脏区（“整屏静态、局部流动”）。
- LVGL 内存池维持 24 KB。
- 三键共用一路 ADC，不可同时按下；所有交互均按单键手势设计。

## 4. 功能需求（Functional Requirements）

| 编号 | 需求 | 验收标准 |
| --- | --- | --- |
| FR-01 | 翻页：`UP` 发送下一页、`DOWN` 发送上一页，走键盘报告 | 在 Windows / macOS / Linux 各系统上，一次按键精确前进或后退一页；每次按键后发空报告释放按键（不卡键） |
| FR-02 | 演示模式全屏切换：`OK` 短按先发 `F5`、再发 `Esc`，设备记忆切换状态 | 三系统上首次 `OK` 短按进入全屏、第二次退出全屏 |
| FR-03 | 一键锁屏：`OK` 长按 ≥800 ms 发送主机档案对应组合键（Windows `Win + L`、macOS `Ctrl + Cmd + Q`、Linux `Super + L`）；仅待机模式可直接触发（媒体模式先经 `OK` 长按返回待机），演示模式不可用；组合键按槽位存 NVS | 三系统主机均真实锁屏；绝不向记录了其它档案的主机发送错误组合 |
| FR-04 | 媒体模式的媒体控制：`UP`/`DOWN` 音量加减、`OK` 短按播放/暂停，走 Consumer Page 报告 | 三系统上音量步进与播放/暂停均生效 |
| FR-05 | 演讲计时器：按 1 Hz 累计已讲时长，每次进入演示模式清零 | 重新进入演示模式显示 00:00；30 分钟演讲累计误差在数秒以内 |
| FR-06 | 三设备槽位串行切换（`MAX_CONNECTIONS=1` 约束）：切换时先优雅断开当前连接（断开后设备按 HOGP 规范进入 suspend 态），再对目标槽位发起定向广播 | 三个已绑定主机间切换全部成功；任何时刻只存在一条连接 |
| FR-07 | 配对：Passkey Entry（屏幕显示 6 位数字）、`PAIRING` 菜单入口、首次开机无绑定时自动进入配对 | 三系统均可完成配对；NVS 为空的首次开机无需任何操作即落入配对模式 |
| FR-08 | 重配对：`CLEAR SLOT` 清空选中槽位；重新配对覆盖旧绑定；用户指南小节写明重配对前主机侧需先“删除设备” | 清空并重配对后旧绑定失效、新绑定可用；用户指南含主机侧删除设备的提醒 |
| FR-09 | 按键音：按键事件发短促提示音，全局可静音，默认关闭 | 发声与静音状态与设置一致；音频失败降级为静音 |
| FR-10 | 背光与睡眠策略：15 秒无操作背光降一档、60 秒熄屏，随后轻睡眠；深睡可达（超时或用户主动） | 实测电流：演示中 15–35 mA；待机 0.2–1 mA；深睡 20–40 µA（520 mAh 电池） |
| FR-11 | 电量显示：界面显示 SOC，并经 Battery Service `0x180F` 暴露给主机 | 主机侧（Windows 设置 / macOS 蓝牙菜单）可见遥控器电量；读值为 `-1` 时优雅降级，不画数字 |
| FR-12 | 页码计数器（本地估算）：进入全屏从 1 计数，翻页 ±1；不显示总页数、不加 “EST” 标注 | 计数与翻页严格同步；重新进入全屏后从 1 重新开始 |

## 5. 输入与输出（Inputs and Outputs）

输入：

- 按键：`bsp_btn_t`（`BSP_BTN_UP` / `BSP_BTN_DOWN` / `BSP_BTN_OK`）× `bsp_btn_ev_t`（`BSP_BTN_PRESS` / `BSP_BTN_CLICK` / `BSP_BTN_DOUBLE` / `BSP_BTN_LONG`），由按键回调派发；回调运行于按键组件的定时器任务上下文（见 [bsp_button.h](../../../components/bsp/include/bsp_button.h)）。
- 电池：`bsp_battery_soc()` 的整数 SOC（`-1` = 不可用）。
- BLE：GAP/GATT 事件——连接、断开、订阅、Passkey 请求、监督超时。
- NVS：启动时读取的配置、槽位元数据与 NimBLE 绑定。

输出：

- HID 键盘报告（8 位修饰键 + 6KRO）与 Consumer Page 报告，以及释放按键的空报告。
- LVGL 界面更新（非 LVGL 上下文一律持 `bsp_lvgl_lock()`）。
- 背光 PWM 档位与屏幕睡眠/唤醒。
- 音频提示音（按键音、配对成功、锁屏、低电量）。
- BLE 广播控制（定向/普通）与连接参数更新。
- NVS 写入（配置、槽位元数据、绑定）。
- 电源状态切换（轻睡眠/深睡眠/唤醒）。

## 6. 状态与按键语义矩阵（State and Key Semantics Matrix）

应用共五种模式：STANDBY、PRESENT、MEDIA、PAIR、SLEEP。其中 STANDBY 自身分两层：待机主页与菜单页。

状态转换：

| 来源 | 触发 | 去向 |
| --- | --- | --- |
| STANDBY（主页） | `UP` 短按 | MENU |
| STANDBY（主页） | 已连接时 `OK` 短按 | PRESENT |
| STANDBY（主页） | 未连接时 `OK` 短按 | PAIR |
| STANDBY（菜单） | 菜单项 `MEDIA MODE` 确认 | MEDIA |
| STANDBY（菜单） | 菜单项 `PAIRING` 确认 | PAIR |
| MENU | `OK` 长按 | STANDBY（主页） |
| PRESENT | `OK` 长按 | STANDBY |
| MEDIA | `OK` 长按 | STANDBY |
| PAIR | `OK` 短按（取消） | STANDBY |
| 任意活动模式 | 电源超时 | SLEEP |
| SLEEP | 任意键 | STANDBY（首键事件被吃掉，不触发功能） |

完整按键语义矩阵：

| 模式 | 按键 | 短按（CLICK） | 双击（DOUBLE） | 长按（≥800 ms） |
| --- | --- | --- | --- | --- |
| STANDBY（主页） | UP | 进入菜单页 | 留空（不绑定动作） | — |
| STANDBY（主页） | DOWN | 切换设备槽位（循环 1→2→3→1，沿用 PAIR 模式的槽位切换语义） | 留空（不绑定动作） | — |
| STANDBY（主页） | OK | 已连接：进入 PRESENT；未连接：进入 PAIR | 留空（不绑定动作） | 锁屏 |
| STANDBY（菜单） | UP / DOWN | 在 8 项菜单中导航 | 留空（不绑定动作） | — |
| STANDBY（菜单） | OK | 进入/确认选中项 | 留空（不绑定动作） | 返回待机主页 |
| PRESENT | UP / DOWN | 下一页 / 上一页 | 留空（不绑定动作） | — |
| PRESENT | OK | 全屏切换（`F5` / `Esc`） | 留空（不绑定动作） | 返回 STANDBY |
| MEDIA | UP / DOWN | 音量 + / 音量 − | 留空（不绑定动作） | — |
| MEDIA | OK | 播放 / 暂停 | 留空（不绑定动作） | 返回 STANDBY |
| PAIR | DOWN | 循环切换槽位（槽位 1 / 2 / 3） | 留空（不绑定动作） | — |
| PAIR | OK | 取消并返回 STANDBY | 留空（不绑定动作） | — |
| SLEEP | 任意 | 仅唤醒（首键事件被吃掉） | — | — |

待机菜单固定 8 项：`PAIRING`、`CLEAR SLOT`、`SLOT`、`HOST PROFILE`、`KEY SOUND`、`BACKLIGHT`、`MEDIA MODE`、`ABOUT`。

锁屏可用范围说明：锁屏功能的适用域为仅待机模式（按决议在演示模式中排除）。仅待机模式可直接触发；媒体模式三键已全部占用、无可用手势，故媒体模式 `OK` 长按先返回 STANDBY，回到待机后即可触发锁屏。

电源子状态机：

```text
ACTIVE --（15 秒无操作）--> DIM --（60 秒无操作）--> SCREEN OFF --> LIGHT SLEEP --> DEEP SLEEP
   ^__________________________ 任意键唤醒（首键吃掉） __________________________|
```

### 与 dev-plan v0.1 的偏差（Deviations from dev-plan v0.1）

以下偏差均为用户确认基线，优先于 dev-plan v0.1：

1. 双击语义：dev-plan 曾将双击绑定为中英切换/黑屏/下一曲；已确认基线将 `BSP_BTN_DOUBLE` 在所有模式下留空，并取消黑屏功能。
2. 纯英文界面：dev-plan 规划双语界面与子集中文字库；已确认基线仅交付英文文字，中文留待后续（见第 11 章）。
3. 演示模式限制：dev-plan 允许演示模式内锁屏（长按）与媒体手势；已确认基线在演示模式禁用锁屏与音量，并将 `OK` 长按改派为“返回待机”。
4. 页码计数器：dev-plan 未定页码来源；已确认基线固定为本地估算——进入全屏从 1 计数，不显示总页数、不加 “EST” 标注。

## 7. 并发与任务（Concurrency and Tasks）

[agent-guide.zh_CN.md](../../development/agent-guide.zh_CN.md) 中“运行时不可破坏的规则”一节的全部不变量在此完整适用。任务布局：

- LVGL port 任务是唯一驱动 `lv_timer_handler` 的任务；其它任何任务操作 `lv_*` 对象必须持有 `bsp_lvgl_lock()`，应用层渲染一律经该锁进行。
- 按键回调运行于按键组件的定时器任务，只入队轻量事件；不做阻塞或重活。
- NimBLE host 任务负责 GAP/GATT 处理；HID 报告由应用侧经 NimBLE API 触发发送。
- 单一应用任务消费统一事件队列（按键语义事件、BLE 事件、定时器事件），驱动状态机与界面。
- `esp_timer` 实例：两个背光超时（15 秒降档、60 秒熄屏）、演讲计时器 1 Hz 滴答、电量轮询 10 秒。
- 页面退出顺序：先停止可能访问 UI 的任务或定时器，再删除 screen 并清空对象指针。

## 8. 持久化（Persistence）

24 KB NVS 分区布局：

| 命名空间 | 内容 | 说明 |
| --- | --- | --- |
| `pp_cfg` | 背光档位、按键音开关、当前槽位、主机档案默认值 | 全局配置 |
| `pp_slot0` .. `pp_slot2` | 绑定地址、主机名、OS 类型、锁屏组合、最后使用时间 | 每槽位一条记录 |
| NimBLE 默认命名空间 | 绑定记录 | 由 `CONFIG_BT_NIMBLE_NVS_PERSIST=y` 启用 |

- 初始化沿用 [demo_radio.c](../../../main/demo_radio.c) 中 `demo_radio_nvs_prepare()` 的“初始化失败不擦除”模式：NVS 初始化失败时降级为内存默认值，绝不触发分区擦除。
- 容量核算：3 槽位 × 约 500 B + NimBLE 绑定 3–6 KB < 24 KB 分区，余量充足。

## 9. 内存预算（Memory Budget）

- presenter 档关闭 Wi-Fi，释放约 30–50 KB RAM。
- LVGL 内存池维持 24 KB，240×20 行单缓冲不动（仓库红线）。
- 所有字体与位图放 Flash（运行期 memory-mapped 读取），RAM 仅承担 LVGL 字体描述符。
- 验收以实测为准：启动后真机 `esp_get_free_heap_size()` 报告的 free heap ≥ 120 KB。
- 任何新图片、字体、网络栈、音频缓存、LVGL buffer 或任务栈都要评估内部 RAM；总空闲堆足够不代表存在足够大的连续内存块。

## 10. 失败降级（Failure Degradation）

| 失败 | 降级策略 |
| --- | --- |
| BLE 断链（监督超时） | 先对已绑定主机定向广播 30 秒，失败后转为普通可发现广播 2 分钟并递减占空比；任意按键立即拉高广播占空比 |
| 电量读取返回 `-1` | 界面优雅降级（不画数字）；Battery Service 停止下发数值 |
| 音频初始化/播放失败 | 按键音与全部提示音降级为静音；功能开关保持原状 |
| 显示初始化失败 | 带日志继续启动；任何依赖界面的功能不阻塞射频路径 |
| 深睡 GPIO0 按键唤醒未在本板验证 | 两条兜底路径：（a）轻睡眠常驻 + 定时广播窗口；（b）依赖独立硬件电源键唤醒 |

## 11. 可扩展性（Extensibility）

- `page_source` 抽象：页码通过接口读取，默认实现为本地估算器；未来 `companion_page_source` 可经 GATT 回传真实页码，界面代码零改动。
- GATT 表预留 1 个 Vendor Service UUID 槽位，供后续伴侣功能使用。
- 主机档案表（OS 类型 → 锁屏组合）为数据驱动，可扩展到新系统或自定义 Linux 桌面组合。
- 界面文字采用索引化字符串表；按索引预留第二张表，后续中文化无需重构界面代码。

## 12. 构建档与仓库契约（Build Profile and Repository Contract）

- presenter 档暂不进入 [tools/validate.sh](../../../tools/validate.sh) 门禁；将其纳入 CI 门禁需独立提案。
- 默认档构建产物在本工作前后必须保持字节级不变。
- [tools/verify_firmware.py](../../../tools/verify_firmware.py) 执行的分区校验契约（factory 体积、`cardid` 与 `recovery` 位置）不受影响。

## 13. 测试策略与验收（Test Strategy and Acceptance）

- 平台无关逻辑——`key_semantics`、`app_fsm`、演讲计时器、页码计数器——不依赖 ESP-IDF/LVGL 实现，并以 host 测试覆盖，沿用 [test_ui_pixel_math.c](../../../tests/test_ui_pixel_math.c) 的纯 assert 模式。
- 三系统（Windows、macOS、Linux）真机验收矩阵：配对 → 翻页 → 全屏切换 → 锁屏 → 断开 → 自动回连，逐系统执行；媒体控制与三槽位切换纳入其中。
- 深睡后经 GPIO0 按键唤醒列为必测项（本板行为未验证，见第 15 章）。
- 量化验收指标：启动后 free heap ≥ 120 KB；演示中 15–35 mA；待机 0.2–1 mA；深睡 20–40 µA（520 mAh 电池）。
- 每次交付按 [agent-guide.zh_CN.md](../../development/agent-guide.zh_CN.md) 的四段式格式报告：`Build: PASS / FAIL / NOT RUN`、`Host tests: ...`、`Device tests: ...`、`Unverified: ...`。

## 14. 合规与决策记录（Compliance and Decision Record）

ui_pixel 合规论证：

- 规则出处为 [agent-guide.zh_CN.md](../../development/agent-guide.zh_CN.md) 的“运行时不可破坏的规则”一节：保留 `ui_pixel` 主题体系的义务，约束的是修改默认演示应用页面的场景。
- 本方案为平行独立构建档：默认应用与 `ui_pixel` 零改动。
- [tools/check_repo.py](../../../tools/check_repo.py) 与 [tools/validate.sh](../../../tools/validate.sh) 均不检查界面主题。
- 已考虑并弃用的替代方案：
  - 就地替换默认演示为遥控器界面：弃用，原因是违反默认应用必须保留 `ui_pixel` 的规则。
  - 在应用落地前预建 `plays/` 条目：弃用，原因是仓库禁止创建没有实际内容的骨架条目。
  - 混合双主题构建（默认像素主题 + 遥控器页面）：弃用，原因是用户为本构建档选择了完整 FUI 身份。
- 任何移除或替换 `ui_pixel` 的决策推迟到独立提案。

## 15. 待决问题（Open Issues）

- 深睡 GPIO0 按键唤醒路径在本板未验证；在实测前适用第 10 章的兜底路径。
- macOS 回连行为（偶发回连慢）需用定向广播做真机实测。
- 像素字体候选的 OFL 授权核实仍未完成，字体入库 `assets/fonts/` 前必须确认。
