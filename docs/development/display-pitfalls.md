<p align="right">
  <a href="display-pitfalls.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Display Layer Pitfall Notes

This page records known pitfalls and their fixes observed while bringing up
the ST7789P3 panel on the FoloToy AI Passport (ESP32-C3, no PSRAM). Each
entry lists the visible symptom, what was tried, the root cause, and the
workaround that finally worked on real hardware.

The notes here are project-local — they do not replace the upstream
`AI_HARDWARE_DEVELOPMENT_GUIDE.md` §5 (panel, mirror control, byte order).
They are written as a developer log, in the spirit of YeatsLiao's
`docs/development/development-log.md` (referenced from upstream issues but
not present on this fork).

## 1. `swap_bytes` batch sensitivity

### Symptom
After flashing the firmware to a new panel batch, the screen boots with a
pale blue / off-white wash instead of the expected deep navy (`#0B1030`)
background. Pixel colors look generally "lifted" — the FUI HUD border and
text are still recognizable but everything else looks bleached.

### Attempt 1 — keep upstream `swap_bytes=true`
- **Result**: pale blue / off-white background.
- **Analysis**: LVGL emits little-endian RGB565; the panel expects
  big-endian over SPI. With `swap_bytes=true`, the LVGL port swaps bytes
  inside `esp_lvgl_port`'s flush callback before pushing pixels, so the
  panel still saw a reversed byte order on this batch.

### Attempt 2 — switch `swap_bytes=false`
- **Result**: deep navy background; FUI tokens render with the expected
  cyan / orange / yellow palette.
- **Root cause**: Different ST7789P3 production batches expect different
  SPI byte orders. The driver source is identical, so there is no code
  change that "fixes" the panel — only a per-batch configuration knob.

### Fix
Promote `swap_bytes` to a Kconfig symbol (`CONFIG_BSP_LCD_SWAP_BYTES`,
default `y`). The current production batch overrides the default in
`sdkconfig.defaults` (`CONFIG_BSP_LCD_SWAP_BYTES=n`), so an off-the-shelf
batch still works without edits. BSP consumers read the flag at panel
init and forward it into the LVGL port configuration.

### Lessons
- `swap_bytes` must be validated against the actual panel via real-device
  bring-up; do not trust the upstream default blindly.
- When you receive a new batch, re-measure with the same test image and
  flip the flag in `sdkconfig.defaults` accordingly.
- Record the current decision in `bsp_pins.h` and / or the
  `Kconfig.projbuild` help text so the next maintainer does not have to
  re-derive it.

## 2. `rotation` change leading to partial rendering

### Symptom
After changing the LVGL port `rotation` to `{swap_xy=true, mirror_y=true}`
to "rotate the panel 90° clockwise + flip vertically", only the top
20 px row of the screen redraws. The rest of the framebuffer keeps the
previous page's content. Rebooting restores a clean frame briefly, then
the same artifact returns.

### Root cause
The rotation flip triggers `lvgl_port_disp_rotation_update()` which
re-issues the ST7789 MADCTL command. The next partial flush ends up with
the new MADCTL applied but the LVGL PARTIAL buffer is still dimensioned
for the old geometry, so chunks 2 and onward are clipped to nothing
visible. The top 20 px row happens to be the first chunk and survives.

### Fix
Roll back rotation to `{false, false, false}` to match the physical
assembly orientation of the panel. The panel is mounted in portrait,
and re-routing the content layout in software is cheaper than fighting
the LVGL port rotation update path on a single-buffered ST7789P3.

### Lessons
- `rotation` must match the screen's physical mounting direction; it is
  not a free re-orientation handle.
- Do not edit `rotation` casually — it changes MADCTL at runtime and
  interacts with `esp_lcd_panel_mirror()`, which is intentionally left
  off in BSP so the LVGL port owns MADCTL.
- On this board (single 240×20 row buffer, `double_buffer=false`)
  partial flushes rely on geometry-stable rotations; future changes
  must be validated against the same PARTIAL flush code path.
