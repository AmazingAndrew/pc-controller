<p align="right">
  <strong>简体中文</strong> · <a href="ci-pitfalls.md">English</a>
</p>

# CI 构建踩坑：从双 sdkconfig 回归上游简洁模式

本文记录 PC Controller（BLE HID PPT 翻页遥控器）固件分支上连续 CI 失败的复盘。
把 8 个具体的坑按时间顺序沉淀下来，方便下一位需要独立构建档的贡献者不再
踩同样的雷。

## 背景与硬约束

PC Controller 是一个 fork 内的本地构建目标：通过 BLE HID 向已配对的 PC 发送
键盘 / 消费控制 / 厂商 HID 报文，驱动 PowerPoint 或 Keynote 翻页、息屏、
音量，以及一次性的演讲计时器。它必须与上游演示固件并存，且不能覆盖演示固件。

两条事实从一开始就把 CI 设计空间卡死了：

- 上游 `FoloToy/ai-passport` 每次 push 构建一份固件，由单一 `sdkconfig.defaults`
  配合 `validate.sh --firmware` 一行命令驱动。CI 模式写在
  `docs/development/CI-build-and-release.md` 与 `docs/development/CI-validation.md`。
- PC Controller 需要一个独立的构建档，不能扰动默认演示构建。

我们没有先通读上游文档，而是自行设计了 **双 sdkconfig 架构**
（`sdkconfig.defaults` + `sdkconfig.presenter.defaults`）。这个选择直接导致
了下面按时间顺序记录的 8 个坑，横跨数小时、连续 **8 次以上 CI 失败**，
最后才停下来回归上游方案并简化。

## 踩坑清单（按时间顺序）

每条坑按"症状 / 根因 / 修复 / 经验"四要素记录。

### 1. Windows Git 丢失可执行权限

- **症状**：首次提交 shell 脚本后 CI 立即报
  `./tools/install-actionlint.sh: Permission denied`（exit 126）。
- **根因**：Windows 上的 Git 提交 shell 脚本时不会保留可执行位（executable bit）。
  上游 checkout 中是 `100755` 的脚本，经 Windows 端 clone / add / push 后变成 `100644`。
- **修复**：`git update-index --chmod=+x tools/install-actionlint.sh tools/validate.sh`
  恢复可执行位，再提交一次 mode 变更。
- **经验**：Windows 上 clone 后必须显式恢复可执行权限。把 `100644` 的 shell
  脚本当成缺陷处理，而不是默认状态。

### 2. SDKCONFIG_DEFAULTS 需要 CMake 分号列表

- **症状**：`SDKCONFIG_DEFAULTS '.../sdkconfig.defaults sdkconfig.presenter.defaults' does not exist`。
- **根因**：CMake 列表使用分号而非空格作为分隔符。空格分隔的值会被当成一条
  单一路径，自然找不到。
- **修复**：把两个文件名之间改成 `;`：
  `-D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.presenter.defaults'`。
- **经验**：CMake 列表格式反直觉——空格是一条路径，分号才是列表。YAML 必须
  用单引号包裹整段值，避免 shell 把 `;` 当成命令分隔符先吃掉。

### 3. esp-idf-ci-action 用 `bash -c '...'` 包裹 command

- **症状**：`command:` 中含单引号（例如为了包裹带空格的值）时
  CI 报 `command not found`（exit 127）。
- **根因**：`espressif/esp-idf-ci-action` 会把 `command:` 输入包成
  `bash -c '...'`。命令里只要出现一个单引号，外层引号就会被提前关闭，
  action 拿到的是一段被截断的 shell 行。
- **修复**：`command:` 中禁用单引号。若值含空格，用 symlink 把路径映射成
  无空格路径，而不是用引号。
- **经验**：`espressif/esp-idf-ci-action` 的 `command:` 里永远不要写单引号。
  任何 `bash -c` 包裹风格的 action 都一样。

### 4. YAML 多行反斜杠续行被破坏

- **症状**：命令片段被当成独立可执行文件（`sdkconfig.presenter.defaults: command not found`、
  或 `set-target: command not found`）。
- **根因**：`espressif/esp-idf-ci-action` 不能正确处理 YAML 块标量配合反斜杠续行。
  它似乎把每一行当成独立命令，于是 `command: |` 加 `\` 续行被拆成片段。
- **修复**：全部改成单行命令。多步用 `&&` 串联。
- **经验**：`command:` 的值保持单行。不要依赖 action 输入里的 shell 行续行。

### 5. 分号被 shell 当成命令分隔符

- **症状**：即使按 CMake 要求改成 `;` 分隔，仍然报
  `sdkconfig.presenter.defaults: command not found`。
- **根因**：CMake 需要 `-D` 值里出现 `;`，但 action 的 `bash -c` 层把 `;`
  解释成 shell 命令分隔符。两种语义冲突。
- **修复**：彻底不用 `;`。要么用 symlink 把多文件路径收敛成单文件路径，
  要么把多份 defaults 合并成一份。
- **经验**：该 action 的命令解析对复杂参数不友好。`command:` 里出现任何
  shell 元字符都是陷阱。

### 6. Docker 挂载卷中的 symlink 不稳

- **症状**：配置阶段（configure）通过，真正的 `idf.py build` 步骤却找不到
  链接目标（`No such file or directory`）。
- **根因**：Docker 卷挂载 + symlink 解析在不同平台与 tmpfs 配置下行为不一致。
  主机上能用的 symlink 在 CI 容器里可能悬空。
- **修复**：改用 `cp` 而不是 `ln -s`，或直接把两份 defaults 合并成一份，
  从源头消除 indirection。
- **经验**：CI Docker 环境里不要依赖 symlink。最简单的修复是把间接层去掉。

### 7. ESP-IDF v5.5.3 / GCC 14 / LVGL v9 API 漂移

- **症状**：ninja 阶段冒出多种编译错误——NVS 隐式函数声明、NimBLE 参数过多、
  LVGL 未声明标识符，以及 `-Werror=enum-compare`（跨枚举比较）。
- **根因**：三条独立的工具链升级叠加：
  - NVS API 改名：`nvs_delete_key` → `nvs_erase_key`。
  - NimBLE 签名收紧：`ble_hs_mbuf_to_flat` 改为 4 参数，
    `BLE_ERR_REM_USER_CONN_TERMINATED` 改名为 `BLE_ERR_REM_USER_CONN_TERM`，
    `appearance_is_complete` 字段改名为 `appearance_is_present`。
  - LVGL v9 移除 `lv_obj_set_style_text_shadow_*`，改用 box shadow API。
- **修复**：更新源码匹配当前 API。本次共改 7 个文件：`pc_storage.c`、
  `pc_app_main.c`、`pc_ble_hid.c`，以及 LVGL v9 shadow 迁移涉及的
  `pc_ui_fui.c`、`pc_ui_fui_media.c`、`pc_ui_fui_present.c`，加
  `sdkconfig.defaults` 中的字体开关。
- **经验**：钉死 ESP-IDF 版本，推送前本地用同一版本编译一遍。本地
  `idf.py build` 几秒就能暴露这些，远快于一轮 CI 分钟级反馈。

### 8. 字体开关放错了 sdkconfig 文件

- **症状**：链接阶段报 `lv_font_unscii_8 undeclared`（编译能过，链接不过）。
- **根因**：`CONFIG_LV_FONT_UNSCII_8=y` 写在 `sdkconfig.defaults`，但 CI
  构建只加载 `sdkconfig.presenter.defaults`，开关根本没进构建。
- **修复**：把字体开关挪到 `sdkconfig.presenter.defaults`（或挪到 CI 真正
  加载的那个文件——理想情况下应该只有一个）。
- **经验**：清楚你的 CI 到底加载了哪些 defaults。如果 CI 根本看不到第二份
  defaults，分两份本身就没有意义。

## 根因分析

把 8 次失败按权重归类：

- **70% 方法论错误**：没读完上游 `CI-build-and-release.md` 就自创双 sdkconfig。
  上游用单一 `sdkconfig.defaults` + `validate.sh --firmware`，足够用。我们自加的
  复杂度毫无必要。
- **20% 工具链版本**：ESP-IDF v5.5.3 / GCC 14 / LVGL v9 的 API 变化是合理的、
  可预期的。本地编译能一次抓完。
- **10% 架构错配**：双 sdkconfig 方案理论上可行，但实际与 kconfig 行为、
  Docker tmpfs 隔离、symlink 处理、以及 `espressif/esp-idf-ci-action` 的
  参数解析语义全面冲突。架构本身在与工具链对抗。

## 正确做法

踩完 8 个坑之后，正确的结构就是上游的结构：

1. 用一份 `sdkconfig.defaults`，把 presenter 专用开关一起放进
   去（或合并两份 defaults）。
2. 构建交给 `validate.sh --firmware`——它已经知道正确的 target、分区表、
   合并步骤。
3. CI 工作流：一个 `espressif/esp-idf-ci-action` 步骤，
   `command: idf.py build`。不要 `set-target`、不要 `SDKCONFIG_DEFAULTS`、
   不要自定义合并、不要 symlink。
4. 如果必须让 presenter 构建与演示构建完全独立，用
   `validate.sh --firmware --profile presenter` 风格的开关，而不是
   再造一份 sdkconfig 文件。

## 可复用的经验

按影响排序，留给下一位贡献者：

1. **先读上游文档**：`docs/development/CI-build-and-release.md` 与
   `docs/development/CI-validation.md` 写明了 fork 应该照搬的 CI 模式。
   没有记录在案的偏离就是几小时的代价。
2. **对齐上游模式**：fork 应尽量减少与上游的架构偏离。每一次偏离都是
   维护税，也是 CI 陷阱。
3. **先本地后 CI**：每次 push 前跑一遍 `validate.sh --firmware`（或对应的
   本地入口）。约 80% 的 CI 失败几秒内就能复现，剩下 20% 才是真正
   需要 CI 环境的。
4. **避免花式 workaround**：symlink、环境变量注入、多文件 SDKCONFIG_DEFAULTS、
   引号转义，在 CI 环境里都很脆弱。上游不用，你也不要用。
5. **一个提交一件事**：把 API 修复、CI 配置、工作流修复拆开。混合提交让
   bisect 与回滚都很痛。
6. **钉死工具链版本**：在 CI 里记录 ESP-IDF、GCC、LVGL 版本（用
   `IDF_VERSION` action 输入），本地用同一版本编译后再 push。

## 归属

这是一份通用性的 fork 开发经验沉淀，提议以文档 PR 的形式回馈到上游
`FoloToy/ai-passport`，避免后来人重复踩这 8 个坑。
