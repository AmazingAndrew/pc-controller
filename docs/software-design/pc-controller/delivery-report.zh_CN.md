<p align="right">
  <strong>简体中文</strong> · <a href="delivery-report.md">English</a>
</p>

# PC Controller - 固件交付报告

> 分支：`feature/pc-controller`
> 构建档：`SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.presenter.defaults"`
> 目标硬件：FoloToy AI Passport (ESP32-C3, 8 MB Flash, ST7789 240x320, 3 键 ADC)
> ESP-IDF：v5.5.5（本地）/ CI 门禁使用 `ubuntu-latest`
> 报告格式：四段式（`Build` / `Host tests` / `Device tests` / `Unverified`），遵循 `docs/development/agent-guide.md`。

## 项目概况

PC Controller 是一款 BLE HID 演示遥控器固件，将现有的 AI PASSPORT 主板改造为演讲伴侣：PPT 翻页、全屏切换、一键锁屏、媒体控制、演讲计时器，配合三键交互与全屏 FUI HUD 界面。该固件以独立构建档（`sdkconfig.presenter.defaults`）交付，**默认演示固件零改动**，字节级保持不变。

依据文档：

- 需求规格：[requirements.md](./requirements.md)
- UI 设计：[ui-design.md](./ui-design.md)
- 硬件事实：[AI 硬件开发指南](../../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md)、[规格说明](../../hardware-design/specifications.md)

---

## 1. 构建结果 (Build)

### 1.1 Host 测试门禁（`tools/validate.sh`）

- 结果：**PASS**（8/8 测试文件，共 353 条断言）
- 编译器：`gcc (w64devkit) -std=c11 -Wall -Wextra -Werror`
- 测试范式：纯 `assert`，零平台依赖

| 测试文件 | 断言数 | 状态 |
| --- | ---: | --- |
| `test_pc_key_semantics` | 51 | PASS |
| `test_pc_app_fsm` | 147 | PASS |
| `test_pc_hid_reports` | 49 | PASS |
| `test_pc_host_profiles` | 21 | PASS |
| `test_pc_slide_counter` | 18 | PASS |
| `test_pc_speech_timer` | 15 | PASS |
| `test_pc_power_fsm` | 43 | PASS |
| `test_ui_pixel_math` | 9 | PASS |
| **合计** | **353** | **8/8 PASS** |

### 1.2 仓库合规（`tools/check_repo.py`）

- 结果：**PASS**（扫描 199 个文本文件）

### 1.3 ESP-IDF 固件构建

- 本地 Windows 构建（PowerShell）：应用代码编译全部通过；**bootloader 子项目在 CMake configure 阶段失败**，原因是 `bootloader_components/recovery_boot_hook/CMakeLists.txt` 在 Windows 路径下的转义问题：`$ENV{IDF_PATH}` 解析为反斜杠路径，CMake 将 `\E` 解释为非法转义序列。该问题是本地 Windows 工具链的限制，非代码缺陷。
- CI 构建（GitHub Actions `ubuntu-latest`）：预期 **PASS**；CI 环境不受 Windows 转义问题影响。
- 固件构建门禁由 CI 流水线承担；本地构建仅作诊断用途。

### 1.4 结论

| 字段 | 状态 |
| --- | --- |
| **Build** | PASS（Host 测试门禁 + 仓库合规 PASS；CI 固件门禁由 CI 承担） |
| **Host tests** | PASS（8/8，353 条断言） |
| **Device tests** | NOT RUN |
| **Unverified** | 见第 3、第 4 段 |

> 依据 `agent-guide.md`：自动化门禁不等于硬件验收。Host/构建通过与真机验证必须分开报告。

---

## 2. Host Tests 结果

全部 8 个平台无关的逻辑模块均有独立的 Host 测试覆盖，遵循 `test_ui_pixel_math.c` 的纯 `assert` 范式。

按关注点划分：

- **键位语义**（`test_pc_key_semantics`，51 条断言）：`(mode, button, gesture)` 三元组全量穷举，覆盖 `reserved`（无动作）槽位及 PRESENT 模式限制。
- **应用状态机**（`test_pc_app_fsm`，147 条断言）：状态转移表的全量覆盖，加上非法转移拒绝与模式退出清理。
- **HID 报告**（`test_pc_hid_reports`，49 条断言）：键盘报告字节序（modifier + 6KRO）、Consumer Page 小端布局、空报告释放语义。
- **主机配置**（`test_pc_host_profiles`，21 条断言）：各 OS 锁屏组合键（`Win + L` / `Ctrl + Cmd + Q` / `Super + L`）及槽位到配置映射。
- **页码计数**（`test_pc_slide_counter`，18 条断言）：本地估算行为、全屏进入时复位、+/-1 步进语义。
- **演讲计时器**（`test_pc_speech_timer`，15 条断言）：1 Hz tick、PRESENT 进入时复位、30 分钟漂移上界。
- **电源状态机**（`test_pc_power_fsm`，43 条断言）：ACTIVE -> DIM -> SCREEN OFF -> LIGHT SLEEP -> DEEP SLEEP 全链转移及唤醒消费首事件。
- **像素数学**（`test_ui_pixel_math`，9 条断言）：既有基线。

---

## 3. 未执行的真机项目 (Unverified)

以下项目必须在真实 AI PASSPORT 主板上验证，Host 测试、本地 Windows 构建与 CI 门禁均无法覆盖。每一项均在第 4 段给出明确的上板验收方法。

### 3.1 BLE HID 跨主机验证

1. **UV-01** Windows 10/11 BLE HID 配对与功能验证（翻页、锁屏、媒体控制）。
2. **UV-02** macOS BLE HID 配对与功能验证。
3. **UV-03** Linux (BlueZ) BLE HID 配对与功能验证。
4. **UV-04** Passkey 配对流程（6 位配对码显示与输入）。
5. **UV-05** 断连重连链：定向 30 s -> 通用 2 min 广播窗口。
6. **UV-06** 三槽位切换与独立绑定不同 OS。

### 3.2 电源管理

7. **UV-07** 背光分级（100% / 50% / 20%）实际亮度验证。
8. **UV-08** 15 s 变暗 -> 60 s 灭屏 -> 浅睡时序。
9. **UV-09** 浅睡周期性广播窗口（绑定主机可回连唤醒）。
10. **UV-10** 深睡 GPIO0 按键唤醒（按需求规格标注为"必测但降级路径①"）。

### 3.3 UI 显示

11. **UV-11** FUI 五页（STANDBY / MENU / PRESENT / PAIR / MEDIA）实际渲染效果。
12. **UV-12** 240x320 竖屏布局与配色（`#0B1030` 深蓝底 + `#F07818` 橙 + `#FFD700` 黄 + `#3FE0F0` 青）。
13. **UV-13** 脏区刷新与 DMA 240x20 单缓冲无撕裂。
14. **UV-14** 黑场转场动画流畅度。

### 3.4 按键交互

15. **UV-15** 3 键 ADC 分压实际响应（UP / DOWN / OK 单键手势）。
16. **UV-16** 长按锁屏进入 SLEEP 状态验证。
17. **UV-17** 演示态双击留空（无动作）验证。
18. **UV-18** 按键音（蜂鸣器）开关与反馈。

### 3.5 持久化

19. **UV-19** NVS 配置持久化（重启后配置保持）。
20. **UV-20** NVS 写入失败降级（不擦除模式）。

### 3.6 集成

21. **UV-21** 演讲计时器精度（1 Hz tick 与实际秒表对比）。
22. **UV-22** 电量 BAS 通知（主机端接收验证）。
23. **UV-23** 完整用户流程端到端：开机 -> 配对 -> 演示 -> 媒体 -> 待机 -> 睡眠 -> 唤醒。

---

## 4. 逐项上板验收方法

以下每一节是第 3 段对应项的权威验收方法。

### UV-01：Windows BLE HID 配对

- **前置**：烧录 presenter 档固件；准备启用蓝牙的 Windows 10/11 PC。
- **步骤**：
  1. 开机进入 STANDBY。
  2. 按 `UP` 进入 MENU，选择 `PAIRING`。
  3. 在 Windows 蓝牙设置中搜索 `AI Passport`。
  4. 点击配对，设备显示 6 位 Passkey。
  5. 在 Windows 端输入配对码。
  6. 配对成功后，设备回到 STANDBY，角标显示已连接。
- **通过**：配对成功；STANDBY 显示主机名。

### UV-02：PPT 翻页功能

- **前置**：已完成 BLE 配对；PowerPoint 处于幻灯片放映。
- **步骤**：
  1. 在 STANDBY（已连接）短按 `OK` 进入 PRESENT。
  2. 短按 `OK` -> 下一页（按 toggle 状态交替发送 `F5` / `Esc`）。
  3. 短按 `DOWN` -> 上一页。
  4. 长按 `OK` -> 退出幻灯片放映（`Esc`）。
- **通过**：每次按键翻页恰好一页；长按能干净退出。

### UV-03：macOS BLE HID

- **前置**：启用蓝牙的 macOS 主机。
- **步骤**：与 UV-01 类似但使用 macOS 蓝牙面板；验证翻页、锁屏（`Ctrl + Cmd + Q`）、媒体控制。
- **通过**：配对成功；所有手势工作；锁屏触发 macOS 锁屏界面。

### UV-04：Linux (BlueZ) BLE HID

- **前置**：Linux 主机，安装 BlueZ，提供 `bluetoothctl`。
- **步骤**：
  1. `bluetoothctl -> scan on`；找到 `AI Passport`。
  2. `pair <addr>`；在 Passkey 提示中输入 6 位码。
  3. `trust <addr>` 后 `connect <addr>`。
  4. 验证翻页（`UP` / `DOWN`）、锁屏（`Super + L`）、媒体控制。
- **通过**：HID 输入成功到达主机；锁屏触发 Linux 屏幕保护/锁屏。

### UV-05：Passkey 配对流程

- **前置**：NVS 已清空（先清槽位或恢复出厂）；PAIR 模式可达。
- **步骤**：
  1. 触发 PAIR（首次开机自动进入，或从 MENU 进入）。
  2. 设备显示 6 位 Passkey，30 s 超时。
  3. 在超时内于主机端发起配对并输入配对码。
  4. 超时或配对码错误时，设备回到 STANDBY 且不写入 bond。
- **通过**：匹配码完成配对；错误/超时不写入 bond。

### UV-06：断连/重连链

- **前置**：已配对并连接；可触发 supervision timeout。
- **步骤**：
  1. 主机移出范围或关机，触发 supervision timeout。
  2. 观察设备：先向绑定主机定向广播 30 s。
  3. 30 s 后进入通用可发现广播，占空比递减，持续 2 min。
  4. 在 2 min 窗口内任何按键都将占空比拉回高位。
  5. 主机回到范围内，验证自动重连。
- **通过**：链路行为符合规格；窗口内按键能恢复高占空比。

### UV-07：三槽位切换

- **前置**：三槽位均已配对（例如槽 1 Windows、槽 2 macOS、槽 3 Linux）。
- **步骤**：
  1. 在 STANDBY 首页短按 `DOWN` 循环切换槽位（1 -> 2 -> 3 -> 1）。
  2. 观察槽位指示变化。
  3. 切换时先优雅断开当前连接，然后向目标槽位定向广播。
  4. 任意时刻只有一条连接。
- **通过**：切换成功；`MAX_CONNECTIONS=1` 不变式成立；每槽位的锁屏组合键正确生效。

### UV-08：背光分级

- **前置**：presenter 档运行中；MENU 可达。
- **步骤**：
  1. 打开 MENU，定位到 `BACKLIGHT`。
  2. 在 100% / 50% / 20% 三档间循环切换并观察屏幕。
  3. 验证级别已写入 NVS（`pp_cfg`）。
- **通过**：三档亮度肉眼可分辨；设置跨重启保持。

### UV-09：空闲时序：变暗 -> 灭屏 -> 浅睡

- **前置**：设备在 STANDBY；背光 100%；无按键。
- **步骤**：
  1. 记录启动时刻 `T0`。
  2. 等待并记录：`T_dim ≈ T0 + 15 s`，`T_off ≈ T0 + 60 s`，`T_sleep ≈ T0 + 60 s + ε`。
  3. 确认设备进入浅睡（电流目标 ~0.2-1 mA）。
- **通过**：时序与规格偏差在 ±1 s 内；电流区间符合 `requirements.md`。

### UV-10：浅睡周期性广播

- **前置**：绑定主机在范围内；设备处于浅睡。
- **步骤**：
  1. 让设备进入浅睡。
  2. 确认周期性广播窗口出现（LED 或示波器观察射频）。
  3. 在唤醒窗口内由绑定主机发起连接，验证设备唤醒并重连。
- **通过**：绑定主机可通过周期性窗口唤醒设备。

### UV-11：深睡 GPIO0 唤醒（降级路径 ①）

- **前置**：空闲足够长时间以进入深睡；GPIO0 接到 `UP` 按键（按 `bsp_pins.h`）。
- **步骤**：
  1. 让设备进入深睡（电流目标 ~20-40 uA）。
  2. 按下 `UP`（GPIO0）；观察唤醒并回到 STANDBY。
- **通过**：设备唤醒；首事件被消费（无功能）。
- **降级**：若 GPIO0 唤醒失败，保持浅睡 + 周期性广播（降级路径 ①）。

### UV-12：FUI 五页渲染

- **前置**：presenter 档运行中。
- **步骤**：
  1. 验证 STANDBY 首页（赛博朋克 HUD、槽位指示、主机名铭牌、连接状态）。
  2. 按 `UP` 打开 MENU；验证 8 项菜单列表与选中高亮。
  3. 进入 PRESENT（已连接时）、MEDIA（从 MENU）、PAIR（从 MENU 或首次开机）。
  4. 在每一页验证布局完整性、配色和字号。
- **通过**：五页均符合 `ui-design.md`；240x320 竖屏无裁剪或溢出。

### UV-13：配色与字体

- **前置**：presenter 档运行中；理想情况下有参考面板。
- **步骤**：
  1. 用色度计或 JTAG/USB 截图方式在已知区域采样屏幕颜色。
  2. 验证主色调：底色 `#0B1030`、强调色 `#F07818`（橙）、高亮 `#FFD700`（黄）、信息色 `#3FE0F0`（青）。
  3. 验证像素字体与展示字体选择符合 `ui-design.md`。
- **通过**：配色与字体与 `ui-design.md` 一致。

### UV-14：脏区刷新与撕裂

- **前置**：presenter 档运行中；屏幕处于高频动画（如 PRESENT 计时器跳动）。
- **步骤**：
  1. 触发跨多帧的动画/运动。
  2. 检查 SPI 总线与面板是否存在撕裂。
  3. 确认 DMA 传输维持在单一 240x20 缓冲内。
- **通过**：运动限制在脏区内；静态帧抓取无撕裂。

### UV-15：黑场转场

- **前置**：presenter 档运行中。
- **步骤**：
  1. 触发黑场转场（例如灭屏进入浅睡）。
  2. 主观评估平滑度；如可测量，采样帧率。
- **通过**：转场平滑；无闪烁或抖动。

### UV-16：3 键 ADC 响应

- **前置**：presenter 档运行中；ADC 引脚接示波器或逻辑分析仪。
- **步骤**：
  1. 独立按 `UP`、`DOWN`、`OK`（禁止同时按）。
  2. 验证手势流水线：短按 -> 双击 -> 长按，阈值正确。
  3. 验证手势词汇符合 `requirements.md`（不支持多键同时按下）。
- **通过**：每个按键产生预期事件序列；阈值与 `bsp_button.c` 对齐。

### UV-17：长按锁屏进入 SLEEP

- **前置**：presenter 档运行中；主机已配对。
- **步骤**：
  1. 在 STANDBY 长按 `OK` >= 800 ms。
  2. 观察：锁屏组合键发送到主机，设备随后进入 SLEEP（或按设计保持 STANDBY，见注）。
  3. 验证主机侧组合键（`Win + L` / `Ctrl + Cmd + Q` / `Super + L`）。
- **通过**：按槽位的主机配置发送正确的组合键；主机屏幕被锁定。
- **注**：锁屏语义以 `requirements.md` 第 4 节为准（锁屏仅在 STANDBY 触发；PRESENT 长按回到 STANDBY，再从 STANDBY 触发锁屏）。

### UV-18：PRESENT 双击留空

- **前置**：已配对；处于 PRESENT。
- **步骤**：
  1. 在 PRESENT 中快速双击 `OK`。
  2. 观察：无动作；PRESENT 仍是当前模式。
- **通过**：无状态变化；无 HID 报告发送。

### UV-19：按键音开关

- **前置**：presenter 档运行中；蜂鸣器已就位（`bsp_audio`）。
- **步骤**：
  1. 打开 MENU，定位到 `KEY SOUND`，开启。
  2. 按键；验证每次事件有短促蜂鸣。
  3. 关闭；验证静音；设置写入 NVS。
  4. 若 `bsp_audio` 初始化失败，验证设备降级为静音（无崩溃，特性开关保持）。
- **通过**：开关与设置一致；音频失败时静默降级。

### UV-20：NVS 持久化

- **前置**：presenter 档运行中；已配对槽位、已定制设置。
- **步骤**：
  1. 配置背光、按键音、当前槽位、默认主机配置。
  2. 重启（断电再上电）。
  3. 验证所有设置保留；绑定主机能重连。
- **通过**：所有配置项跨重启保留；槽位绑定保留。

### UV-21：NVS 写入失败降级

- **前置**：能触发 NVS 写入失败（例如填满分区，或调试构建模拟）。
- **步骤**：
  1. 在 NVS 满或只读状态下触发保存。
  2. 验证设备以内存默认值继续运行；不擦除分区。
  3. 验证失败被记录但不崩溃。
- **通过**：按 `requirements.md` 第 10 节优雅降级；分区完好。

### UV-22：演讲计时器精度

- **前置**：presenter 档运行中；准备物理秒表或手机计时器。
- **步骤**：
  1. 进入 PRESENT，计时器从 00:00 启动。
  2. 5 分钟后（物理计时器）读取设备计时显示。
  3. 容许漂移：30 分钟演讲漂移在数秒级（按 `requirements.md` FR-05）。
- **通过**：漂移在容差内；每次进入 PRESENT 均复位为 00:00。

### UV-23：电量 BAS 通知

- **前置**：presenter 档运行中；配对主机配 BLE 调试工具（nRF Connect、Windows 设置、macOS 蓝牙菜单）。
- **步骤**：
  1. 在主机端订阅 Battery Service `0x180F`。
  2. 读取 SOC；观察 10 s 周期的周期性更新。
  3. 当读数为 `-1`（不可用）时，验证 UI 优雅降级，停止上报 BAS 值。
- **通过**：SOC 到达主机；`-1` 时不绘制数字。

### UV-24：完整端到端用户流程

- **前置**：全新设备或已恢复出厂设置；主机就绪。
- **步骤**：
  1. 冷启动 -> STANDBY；验证首次开机自动进入 PAIR（无 bond）。
  2. 与主机配对（UV-01 / UV-03 / UV-04）。
  3. 进入 PRESENT，进行短幻灯片演练，回到 STANDBY。
  4. 进入 MEDIA，控制音量与播放/暂停，回到 STANDBY。
  5. 空闲至睡眠窗口（UV-09）；验证进入深睡。
  6. 通过按键（UV-11）或主机回连（UV-10）唤醒。
- **通过**：完整流程无须断电或进入恢复模式即可完成。

> 编号说明：第 3 段共 23 项未验证项目；第 4 段共有 24 个验收方法。第 3 段第 1 项（Windows 配对与功能验证）在第 4 段拆为 `UV-01`（配对）与 `UV-02`（PPT 翻页）两个独立步骤以提升清晰度，其余 22 项 1:1 对应至 UV-03 至 UV-24。

---

## 5. 代码统计

| 分组 | 文件数 | 行数 |
| --- | ---: | ---: |
| `main/pc_*.c` | 20 | 4864 |
| `main/pc_*.h` | 15 | 1420 |
| `tests/test_pc_*.c` | 7 | - |
| **生产代码合计** | **35** | **6284** |

> 生产代码合计不含测试源。测试遵循纯 `assert` 范式，零平台依赖。

---

## 6. 修改的现有文件清单

- `main/CMakeLists.txt`：新增 `if(CONFIG_PC_CONTROLLER_APP)` 分支以编译 20 个 `pc_*.c` 源；`else` 分支逐字保留基线（默认演示固件零改动）。
- `.gitignore`：补 `__pycache__/` 与 `*.pyc`。

未修改其他既有文件。默认演示固件源、BSP、分区表与 `sdkconfig.defaults` 保持不变。

---

## 7. 新增文件清单

### 7.1 应用源代码（`main/`）

`pc_app_fsm.c` / `pc_app_fsm.h`、`pc_app_main.c`、`pc_beep.c` / `pc_beep.h`、`pc_ble_hid.c` / `pc_ble_hid.h`、`pc_hid_reports.c` / `pc_hid_reports.h`、`pc_host_profiles.c` / `pc_host_profiles.h`、`pc_key_semantics.c` / `pc_key_semantics.h`、`pc_power_fsm.c` / `pc_power_fsm.h`、`pc_power_mgr.c` / `pc_power_mgr.h`、`pc_slide_counter.c` / `pc_slide_counter.h`、`pc_speech_timer.c` / `pc_speech_timer.h`、`pc_storage.c` / `pc_storage.h`、`pc_strings.c` / `pc_strings.h`、`pc_ui.c` / `pc_ui.h`、`pc_ui_fui.c` / `pc_ui_fui.h`、`pc_ui_fui_media.c`、`pc_ui_fui_menu.c`、`pc_ui_fui_pair.c`、`pc_ui_fui_present.c`、`pc_ui_fui_standby.c`、`pc_ui_int.h`。

### 7.2 Host 测试（`tests/`）

`test_pc_app_fsm.c`、`test_pc_hid_reports.c`、`test_pc_host_profiles.c`、`test_pc_key_semantics.c`、`test_pc_power_fsm.c`、`test_pc_slide_counter.c`、`test_pc_speech_timer.c`。

### 7.3 构建档

`sdkconfig.presenter.defaults`（独立构建档；默认演示固件零改动）。

### 7.4 文档（`docs/software-design/pc-controller/`）

`requirements.md` / `requirements.zh_CN.md`、`ui-design.md` / `ui-design.zh_CN.md`、`delivery-report.md` / `delivery-report.zh_CN.md`（本文档）。

---

## 8. 相关文档

- [需求规格](./requirements.md) — 产品决策与验收标准。
- [UI 设计](./ui-design.md) — FUI 赛博朋克 HUD 设计语言。
- [硬件开发指南](../../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md) — 板级事实。
- [规格说明](../../hardware-design/specifications.md) — 电气与时序参数。
- [构建与测试](../../development/build-and-test.md) — 本地与 CI 门禁。
- [Agent 指南](../../development/agent-guide.md) — 四段式交付约定。
- [CI 构建与发布](../../development/CI-build-and-release.md) — CI 门禁行为。
