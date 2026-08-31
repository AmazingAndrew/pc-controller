<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

<p align="center">
  <a href="https://github.com/AmazingAndrew/pc-controller/actions/workflows/build-presenter.yml"><img src="https://github.com/AmazingAndrew/pc-controller/actions/workflows/build-presenter.yml/badge.svg" alt="CI"></a>
  <a href="https://github.com/AmazingAndrew/pc-controller/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License"></a>
</p>

# PC Controller — FoloToy AI Passport 固件

将 AI PASSPORT 板卡变成 BLE HID 演示遥控器，搭配赛博朋克 FUI HUD 界面。

<!-- TODO: 添加 FUI HUD 截图至 docs/assets/pc-controller/screenshot.png -->

## 功能特性

| 功能 | 说明 |
|---------|-------------|
| PPT 翻页 | UP/DOWN 经 BLE HID 切换幻灯片 |
| 全屏切换 | OK 短按交替发送 F5 / Esc |
| 一键锁屏 | OK 长按（≥800 ms）发送操作系统对应的锁屏组合键 |
| 媒体控制 | UP/DOWN 调节音量，OK 播放/暂停 |
| 演讲计时 | 进入 Present 模式自动归零，1 Hz 滴答 |
| 3 个设备槽位 | 单连接串行切换，独立主机档案 |
| FUI 赛博朋克 HUD | Nicolas Lopardo 风格深海军蓝 + 橙色辉光 |
| 电源管理 | 背光渐暗 → 熄屏 → 浅睡眠 → 深睡眠 |

## 硬件要求

- **主板**：FoloToy AI PASSPORT（ESP32-C3，8 MB Flash，无 PSRAM）
- **显示屏**：ST7789 240×320 RGB565
- **按键**：三键 ADC（UP / DOWN / OK）
- 无需任何硬件改动

## 按键映射

| 按键 | 短按 | 长按（≥800 ms） |
|--------|-------------|---------------------|
| UP | 下一页 / 音量+ | — |
| DOWN | 上一页 / 音量- | — |
| OK | 切换全屏 / 播放-暂停 | 一键锁屏 |

### 主机锁屏档案

| 操作系统 | 组合键 |
|----|----------------|
| Windows | Win + L |
| macOS | Ctrl + Cmd + Q |
| Linux | Super + L |

## 构建

需要 ESP-IDF v5.5.3 或更高版本。

```bash
# 全量验证（主机测试 + 固件构建）
./tools/validate.sh --static   # 主机测试 + 代码风格检查
./tools/validate.sh --firmware # 固件构建 + 合并 + 校验

# 或直接构建
idf.py set-target esp32c3
idf.py build
```

输出：`build/FoloToy-AI-Passport-full.bin`（合并镜像）

## 烧录

```bash
idf.py -p PORT flash
```

或使用 [WebSerial 烧录工具](https://ai-passport.folotoy.cn/tools/web-flasher/)。

## 固件安装与刷机

完整刷机指南请参考[无线安装玩法指南](https://ai-passport.folotoy.cn/guides/wireless-install-plays/)。

### 首次初始化（USB 完整恢复）

1. 通过 USB-C 线将开发板连接电脑
2. 打开 [Chrome/Edge 浏览器刷机工具](https://ai-passport.folotoy.cn/tools/web-flasher/)
3. 刷入合并镜像：`build/FoloToy-AI-Passport-full.bin`
4. 等待"恢复默认固件"流程完成

> 合并镜像包含 bootloader + 分区表 + 应用固件 + 保护分区占位。

### 无线刷机（微信小程序下发）

首次初始化完成后，可通过蓝牙无线安装新玩法固件，无需 USB：

1. 关闭设备电源
2. 同时按住**电源键 + 上键**约 5 秒，进入"系统升级模式"（Recovery Mode）
3. 屏幕显示 6 位配对验证码
4. 打开微信小程序，通过蓝牙连接设备并输入验证码
5. 新固件自动下载并刷入

### 开发调试刷机

仅用于迭代开发：

```bash
idf.py -p PORT flash monitor
```

> 注意：此方式仅刷入应用二进制文件。正式使用或小程序兼容场景请使用合并镜像 `full.bin`。

## 首次开机

1. 上电 → STANDBY 待机屏
2. 按 OK → 进入 MENU → 选择 "Pair"
3. 屏幕显示 6 位配对码
4. 在主机上输入配对码完成 BLE 配对
5. 绑定信息写入 NVS，自动重连

## 验证状态

- **Build**：PASS — 8 项主机测试，353 条断言，CI 固件构建通过
- **Host tests**：PASS — `test_pc_app_fsm`（147）、`test_pc_key_semantics`（51）、`test_pc_hid_reports`（49）、`test_pc_host_profiles`（21）、`test_pc_slide_counter`（18）、`test_pc_speech_timer`（15）、`test_pc_power_fsm`（43）、`test_ui_pixel_math`（9）
- **Device tests**：NOT RUN — 真机验证未执行，详见 [交付报告](docs/software-design/pc-controller/delivery-report.md) §4 的 23 项未验收清单

## 文档

- [需求规格说明书](docs/software-design/pc-controller/requirements.md)
- [UI 设计](docs/software-design/pc-controller/ui-design.md)
- [交付报告](docs/software-design/pc-controller/delivery-report.md)
- [CI 踩坑经验](docs/experiences/AmazingAndrew/ci-pitfalls.md)

## 致谢

派生自 [FoloToy AI Passport](https://github.com/FoloToy/ai-passport) —— 开放的可穿戴 AI 硬件平台。

## 许可证

MIT License，Copyright (c) 2026 FoloToy