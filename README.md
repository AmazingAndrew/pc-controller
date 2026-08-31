<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# PC Controller for FoloToy AI Passport

Turn your AI PASSPORT board into a BLE HID presentation remote with a cyberpunk FUI HUD interface.

<!-- TODO: Add FUI HUD screenshot at docs/assets/pc-controller/screenshot.png -->

## Features

| Feature | Description |
|---------|-------------|
| PPT Remote | UP/DOWN to flip slides via BLE HID |
| Fullscreen Toggle | OK short press sends F5/Esc |
| Lock Screen | OK long press (≥800 ms) sends OS-specific lock combo |
| Media Control | UP/DOWN = volume, OK = play/pause |
| Speech Timer | Auto-starts in Present mode, 1 Hz tick |
| 3 Device Slots | Serial connection, independent host profiles |
| FUI Cyberpunk HUD | Nicolas Lopardo-style deep navy + orange glow |
| Power Management | Backlight dim → screen off → light sleep → deep sleep |

## Hardware Requirements

- **Board**: FoloToy AI PASSPORT (ESP32-C3, 8 MB Flash, no PSRAM)
- **Display**: ST7789 240×320 RGB565
- **Buttons**: 3-key ADC (UP / DOWN / OK)
- No hardware modifications needed

## Button Mapping

| Button | Short Press | Long Press (≥800 ms) |
|--------|-------------|---------------------|
| UP | Next slide / Vol+ | — |
| DOWN | Previous slide / Vol- | — |
| OK | Fullscreen toggle / Play-Pause | Lock screen |

### Host Lock-Screen Profiles

| OS | Key Combination |
|----|----------------|
| Windows | Win + L |
| macOS | Ctrl + Cmd + Q |
| Linux | Super + L |

## Build

Requires ESP-IDF v5.5.3 or later.

```bash
# Full validation (host tests + firmware build)
./tools/validate.sh --static   # host tests + lint
./tools/validate.sh --firmware # firmware build + merge + verify

# Or build directly
idf.py set-target esp32c3
idf.py build
```

Output: `build/FoloToy-AI-Passport-full.bin` (merged image)

## Flash

```bash
idf.py -p PORT flash
```

Or use the [WebSerial flasher](https://ai-passport.folotoy.cn/tools/web-flasher/).

## Firmware Installation

For the complete flashing guide, refer to the [Wireless Install Plays Guide](https://ai-passport.folotoy.cn/guides/wireless-install-plays/).

### First-Time Setup (USB Recovery)

1. Connect the board to your computer via USB-C
2. Open [Chrome/Edge browser flasher](https://ai-passport.folotoy.cn/tools/web-flasher/)
3. Flash the merged firmware image: `build/FoloToy-AI-Passport-full.bin`
4. Wait for the "Restore Default Firmware" process to complete

> The merged image includes bootloader + partition table + application + protected partition placeholders.

### Wireless Update (WeChat Mini Program)

After first-time initialization, you can install new firmware wirelessly via BLE:

1. Power off the device
2. Hold **Power button + UP button** for ~5 seconds to enter Recovery Mode
3. The screen displays a 6-digit pairing code
4. Open the WeChat mini program and connect via BLE using the displayed code
5. The new firmware is downloaded and flashed automatically

### Development Flashing

For iterative development only:

```bash
idf.py -p PORT flash monitor
```

> Note: This flashes the app-only binary. Use the merged `full.bin` for production or mini-program compatibility.

## First Boot

1. Power on → STANDBY screen
2. Press OK → MENU → select "Pair"
3. 6-digit passkey displayed on screen
4. Enter passkey on host to complete BLE pairing
5. Bond info persisted to NVS for auto-reconnect

## Validation Status

- **Build**: PASS — 8 host tests, 353 assertions, CI firmware build green
- **Host tests**: PASS — `test_pc_app_fsm` (147), `test_pc_key_semantics` (51), `test_pc_hid_reports` (49), `test_pc_host_profiles` (21), `test_pc_slide_counter` (18), `test_pc_speech_timer` (15), `test_pc_power_fsm` (43), `test_ui_pixel_math` (9)
- **Device tests**: NOT RUN — see [delivery report](docs/software-design/pc-controller/delivery-report.md) §4 for 23 unverified items

## Documentation

- [Requirements specification](docs/software-design/pc-controller/requirements.md)
- [UI design](docs/software-design/pc-controller/ui-design.md)
- [Delivery report](docs/software-design/pc-controller/delivery-report.md)
- [CI pitfalls experience](docs/experiences/AmazingAndrew/ci-pitfalls.md)

## Acknowledgments

Forked from [FoloToy AI Passport](https://github.com/FoloToy/ai-passport) — open wearable AI hardware platform.

## License

MIT License, Copyright (c) 2026 FoloToy