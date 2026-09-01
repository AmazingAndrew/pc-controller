<p align="right">
  <strong>简体中文</strong> · <a href="display-pitfalls.md">English</a>
</p>

# 显示层踩坑记录

本页记录在 FoloToy AI Passport（ESP32-C3，无 PSRAM）上点亮 ST7789P3
面板过程中遇到的真实坑点与最终修复。每条按"症状 → 尝试 → 根因 →
最终方案 → 经验教训"组织。

本页为 fork 内部笔记，不替代上游 `AI_HARDWARE_DEVELOPMENT_GUIDE.md`
§5 中关于面板、mirror 控制、字节序的硬规范。文档风格参考 YeatsLiao
的 `docs/development/development-log.md`（在上游 issue 中被引用但本
fork 未自带）。

## 1. `swap_bytes` 按批次差异

### 现象
换批次后真机刷机，屏幕点亮呈浅蓝/米白色一片，与预期深蓝
（`#0B1030`）背景完全不符。FUI 边框与文字仍能识别，但其余色块
整体偏白、对比度塌陷。

### 尝试 1：保持上游 `swap_bytes=true`
- **结果**：浅蓝/米白色背景。
- **分析**：LVGL 输出小端 RGB565，面板经 SPI 期望大端。
  `swap_bytes=true` 让 `esp_lvgl_port` 的 flush 回调内部做一次字节
  交换后再推，但当前批次收到的字节序仍是反的。

### 尝试 2：改为 `swap_bytes=false`
- **结果**：背景回到深蓝，FUI 调色板（青/橙/黄）颜色全部正确。
- **根因**：不同批次 ST7789P3 对 SPI 字节序的期望不同，驱动源码
  无法区分；只能用每个批次的配置开关覆盖。

### 修复
把 `swap_bytes` 提升为 Kconfig 符号 `CONFIG_BSP_LCD_SWAP_BYTES`
（默认 `y`）。当前量产批次在 `sdkconfig.defaults` 覆盖为
`CONFIG_BSP_LCD_SWAP_BYTES=n`，使未配置的批次仍能直接工作。
BSP 在面板初始化时读取该开关并写入 LVGL port 配置。

### 经验
- `swap_bytes` 必须经真机实测验证，不要盲信上游默认值。
- 来新批次就要换一张测试图重新校验，必要时翻转 `sdkconfig.defaults`
  中的配置。
- 把当前决策记录在 `bsp_pins.h` 与/或 `Kconfig.projbuild` 的 help
  文案里，省得下一位维护者重新推导。

## 2. 修改 `rotation` 导致仅部分渲染

### 现象
为"逆时针 90° + Y 镜像"调整 LVGL port 的 `rotation` 为
`{swap_xy=true, mirror_y=true}` 后，屏幕只有顶部 20 行刷出新内容，
其余区域仍保留上一帧。重启短暂恢复正常，但同一异常很快复现。

### 根因
`rotation` 切换触发了 `lvgl_port_disp_rotation_update()` 重新下发
ST7789 MADCTL。此时部分刷新仍按旧几何尺寸分配缓冲区，新 MADCTL
生效后第 2 块及之后被裁掉，恰好第一块（顶 20 行）能正常显示。

### 修复
回滚 `rotation` 为 `{false, false, false}`，与面板的物理装配方向
一致。本板是竖屏装机，软件侧重新排版比折腾 LVGL port 的 rotation
更新路径便宜得多。

### 经验
- `rotation` 必须与屏幕物理装配方向对齐，它不是"自由调整方向"的
  旋钮。
- 不要轻易改 `rotation`：它在运行时会写 MADCTL，并与
  `esp_lcd_panel_mirror()` 交互；本 BSP 故意把后者关掉让 LVGL
  port 单独负责 MADCTL。
- 本板只有一块 240×20 行缓冲且 `double_buffer=false`，PARTIAL
  flush 依赖稳定的几何尺寸；后续任何改动都应重新走 PARTIAL
  flush 路径验证。
