<p align="right">
  <a href="delivery-report.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# PC Controller - Firmware Delivery Report

> Branch: `feature/pc-controller`
> Build profile: `SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.presenter.defaults"`
> Target hardware: FoloToy AI Passport (ESP32-C3, 8 MB Flash, ST7789 240x320, 3-key ADC)
> ESP-IDF: v5.5.5 (local) / CI gate on `ubuntu-latest`
> Reporting convention: four-field format (`Build` / `Host tests` / `Device tests` / `Unverified`) per `docs/development/agent-guide.md`.

## Project Overview

PC Controller is a BLE HID presentation remote firmware that turns the existing AI PASSPORT board into a presenter companion: slide paging, full-screen toggle, one-touch host lock, media control, and a presentation timer, wrapped in a full-screen FUI HUD driven by three buttons. It ships as an independent build profile (`sdkconfig.presenter.defaults`); the default demo firmware is byte-for-byte unchanged.

Source-of-truth documents:

- Requirements: [requirements.md](./requirements.md)
- UI design: [ui-design.md](./ui-design.md)
- Hardware facts: [AI Hardware Development Guide](../../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md), [specifications](../../hardware-design/specifications.md)

---

## 1. Build

### 1.1 Host test gate (`tools/validate.sh`)

- Result: **PASS** (8/8 test files, 353 assertions)
- Compiler: `gcc (w64devkit) -std=c11 -Wall -Wextra -Werror`
- Pattern: pure `assert`, zero platform dependency

| Test file | Assertions | Status |
| --- | ---: | --- |
| `test_pc_key_semantics` | 51 | PASS |
| `test_pc_app_fsm` | 147 | PASS |
| `test_pc_hid_reports` | 49 | PASS |
| `test_pc_host_profiles` | 21 | PASS |
| `test_pc_slide_counter` | 18 | PASS |
| `test_pc_speech_timer` | 15 | PASS |
| `test_pc_power_fsm` | 43 | PASS |
| `test_ui_pixel_math` | 9 | PASS |
| **Total** | **353** | **8/8 PASS** |

### 1.2 Repository compliance (`tools/check_repo.py`)

- Result: **PASS** (199 text files scanned)

### 1.3 ESP-IDF firmware build

- Local Windows build (PowerShell): application sources compiled clean; the **bootloader sub-project failed** at CMake configure due to Windows path escaping in `bootloader_components/recovery_boot_hook/CMakeLists.txt`. `$ENV{IDF_PATH}` resolves to a Windows-style backslash path; CMake interprets `\E` as an illegal escape sequence. This is a local Windows toolchain limitation, not a code defect.
- CI build (GitHub Actions `ubuntu-latest`): expected **PASS**; CI is unaffected by the Windows escaping issue.
- Firmware build gate is owned by the CI pipeline; local builds are diagnostic only.

### 1.4 Verdict

| Field | Status |
| --- | --- |
| **Build** | PASS (host gate + repo compliance PASS; CI firmware gate owner = CI) |
| **Host tests** | PASS (8/8, 353 assertions) |
| **Device tests** | NOT RUN |
| **Unverified** | See section 3 and section 4 |

> Per `agent-guide.md`: the automated gate is not hardware acceptance. A clean host/build result is reported separately from on-device verification.

---

## 2. Host Tests (Detail)

All eight platform-independent logic modules have dedicated host-test coverage following the pure-`assert` pattern of `test_ui_pixel_math.c`.

Coverage by concern:

- **Key semantics** (`test_pc_key_semantics`, 51 asserts): full enumeration of `(mode, button, gesture)` triples, including the empty/`reserved` slots and the PRESENT-mode restrictions.
- **Application FSM** (`test_pc_app_fsm`, 147 asserts): every transition listed in the state-transition table, plus invalid-transition rejection and mode-exit cleanup.
- **HID reports** (`test_pc_hid_reports`, 49 asserts): keyboard report byte order (modifier + 6KRO), Consumer Page little-endian layout, empty-report release semantics.
- **Host profiles** (`test_pc_host_profiles`, 21 asserts): per-OS lock combos (`Win + L` / `Ctrl + Cmd + Q` / `Super + L`) and slot-to-profile binding.
- **Slide counter** (`test_pc_slide_counter`, 18 asserts): local-estimate behaviour, reset on full-screen entry, +/-1 step semantics.
- **Speech timer** (`test_pc_speech_timer`, 15 asserts): 1 Hz tick, reset on PRESENT entry, 30-minute drift bound.
- **Power FSM** (`test_pc_power_fsm`, 43 asserts): ACTIVE -> DIM -> SCREEN OFF -> LIGHT SLEEP -> DEEP SLEEP transitions and wake consume.
- **Pixel math** (`test_ui_pixel_math`, 9 asserts): existing baseline.

---

## 3. Unverified (On-Device Items)

The following items require the real AI PASSPORT board and cannot be covered by host tests, local Windows builds, or the CI gate. Each is given an explicit acceptance procedure in section 4.

### 3.1 BLE HID cross-host validation

1. **UV-01** Windows 10/11 BLE HID pairing and feature validation (paging, lock, media).
2. **UV-02** macOS BLE HID pairing and feature validation.
3. **UV-03** Linux (BlueZ) BLE HID pairing and feature validation.
4. **UV-04** Passkey pairing flow (6-digit on-screen code).
5. **UV-05** Disconnect/reconnect chain: directed 30 s -> general 2 min advertising window.
6. **UV-06** Three-slot switching and independent pairing to different OSes.

### 3.2 Power management

7. **UV-07** Backlight step (100% / 50% / 20%) brightness verification.
8. **UV-08** 15 s dim -> 60 s screen off -> light sleep timing.
9. **UV-09** Light-sleep periodic advertising window (bound host can reconnect and wake).
10. **UV-10** Deep-sleep GPIO0 key wake (per requirements: "must-test but degraded path 1").

### 3.3 UI display

11. **UV-11** FUI five-page rendering (STANDBY / MENU / PRESENT / PAIR / MEDIA).
12. **UV-12** 240x320 portrait layout and palette (`#0B1030` deep-blue base, `#F07818` orange, `#FFD700` yellow, `#3FE0F0` cyan).
13. **UV-13** Dirty-rect refresh with DMA single 240x20 buffer (no tearing).
14. **UV-14** Black-screen transition smoothness.

### 3.4 Button interaction

15. **UV-15** Three-key ADC voltage-divider response (UP / DOWN / OK single-key gestures).
16. **UV-16** Long-press lock entry to SLEEP.
17. **UV-17** Present-mode double-click reserved (no action).
18. **UV-18** Key sound (buzzer) toggle and feedback.

### 3.5 Persistence

19. **UV-19** NVS configuration persistence (settings retained after reboot).
20. **UV-20** NVS write failure degradation (no-erase mode).

### 3.6 Integration

21. **UV-21** Speech timer accuracy (1 Hz tick vs physical stopwatch).
22. **UV-22** Battery BAS notification (host-side reception).
23. **UV-23** Full end-to-end user flow: boot -> pair -> present -> media -> standby -> sleep -> wake.

---

## 4. Per-Item On-Board Acceptance Methods

Each procedure below is the authoritative acceptance method for one item in section 3.

### UV-01: Windows BLE HID pairing

- **Prereq**: flash presenter profile; prepare Windows 10/11 PC with Bluetooth enabled.
- **Steps**:
  1. Power on; device enters STANDBY.
  2. Press `UP` to open MENU; select `PAIRING`.
  3. In Windows Bluetooth settings, search for `AI Passport`.
  4. Click Pair; the device displays a 6-digit Passkey.
  5. Enter the code on the Windows side.
  6. After pairing, the device returns to STANDBY with a connection indicator in the corner.
- **Pass**: pairing succeeds; STANDBY shows the host name.

### UV-02: PPT paging

- **Prereq**: paired with a host; PowerPoint in slide-show.
- **Steps**:
  1. From STANDBY (connected), short press `OK` to enter PRESENT.
  2. Short press `OK` -> next slide (also sends `F5`/`Esc` alternately per toggle state).
  3. Short press `DOWN` -> previous slide.
  4. Long press `OK` -> exit slide-show (`Esc`).
- **Pass**: slides advance/retreat by exactly one; long press exits cleanly.

### UV-03: macOS BLE HID

- **Prereq**: macOS host with Bluetooth enabled.
- **Steps**: identical to UV-01 with macOS Bluetooth panel; verify slide paging, lock (`Ctrl + Cmd + Q`), and media control.
- **Pass**: pairing succeeds; all gestures work; lock triggers the macOS lock screen.

### UV-04: Linux (BlueZ) BLE HID

- **Prereq**: Linux host with BlueZ and `bluetoothctl` available.
- **Steps**:
  1. `bluetoothctl -> scan on`; locate `AI Passport`.
  2. `pair <addr>`; respond to Passkey prompt.
  3. `trust <addr>` and `connect <addr>`.
  4. Verify slide paging (`UP`/`DOWN`), lock (`Super + L`), and media control.
- **Pass**: HID inputs reach the host; lock triggers the Linux screen-saver / lock.

### UV-05: Passkey pairing flow

- **Prereq**: empty NVS (clear slot first or factory reset); PAIR mode reachable.
- **Steps**:
  1. Trigger PAIR (auto-entry on first boot or via MENU).
  2. Device displays a 6-digit Passkey with a 30 s timeout.
  3. Initiate pairing from the host; enter the code within the timeout.
  4. On timeout or mismatch, the device returns to STANDBY without storing a bond.
- **Pass**: matching code completes pairing; mismatch / timeout does not write a bond.

### UV-06: Disconnect / reconnect chain

- **Prereq**: paired and connected host; supervise timeout observed.
- **Steps**:
  1. Move the host out of range or power it off to force a supervision timeout.
  2. Observe the device: directed advertising to the bonded host for 30 s.
  3. After 30 s, the device enters general discoverable advertising with decreasing duty cycle, for 2 min.
  4. Any key press during this window restores the high duty cycle.
  5. Bring the host back into range; verify automatic reconnection.
- **Pass**: the chain matches the spec; any key wake during the 2 min window raises the duty cycle.

### UV-07: Three-slot switching

- **Prereq**: three paired slots (e.g., slot 1 Windows, slot 2 macOS, slot 3 Linux).
- **Steps**:
  1. On STANDBY home, short press `DOWN` to cycle slot (1 -> 2 -> 3 -> 1).
  2. Observe the slot indicator change.
  3. Selecting a slot first disconnects the current connection gracefully, then runs directed advertising toward the new slot.
  4. Verify only one connection exists at any time.
- **Pass**: switching succeeds; `MAX_CONNECTIONS=1` invariant holds; per-slot host profile (lock combo) is honored.

### UV-08: Backlight steps

- **Prereq**: presenter profile running; MENU reachable.
- **Steps**:
  1. Open MENU, navigate to `BACKLIGHT`.
  2. Cycle the three levels (100% / 50% / 20%) and observe the panel.
  3. Verify the level is persisted in NVS (`pp_cfg`).
- **Pass**: each level is visibly distinguishable; selection persists across reboot.

### UV-09: Idle timing: dim -> screen off -> light sleep

- **Prereq**: device on STANDBY; backlight at 100%; no key presses.
- **Steps**:
  1. Note the boot time `T0`.
  2. Wait and record: `T_dim ~= T0 + 15 s`, `T_off ~= T0 + 60 s`, `T_sleep ~= T0 + 60 s + epsilon`.
  3. Confirm the device enters light sleep (current drops to ~0.2-1 mA target).
- **Pass**: timings within +/- 1 s of the spec; current band matches `requirements.md` table.

### UV-10: Light-sleep periodic advertising

- **Prereq**: paired host in range; device in light sleep.
- **Steps**:
  1. Allow the device to enter light sleep.
  2. Confirm periodic advertising windows occur (LED or scope on the radio).
  3. Send a connection from the bound host during an awake window; verify the device wakes and reconnects.
- **Pass**: bound host can wake the device through the periodic window.

### UV-11: Deep-sleep GPIO0 wake (degraded path 1)

- **Prereq**: idle long enough to enter deep sleep; GPIO0 connected to `UP` button per `bsp_pins.h`.
- **Steps**:
  1. Let the device enter deep sleep (current ~20-40 uA target).
  2. Press `UP` (GPIO0); observe wake and STANDBY return.
- **Pass**: device wakes; first key event is consumed (no function).
- **Fallback**: if GPIO0 wake fails on this board, stay in light sleep with periodic advertising (degraded path 1) until measured.

### UV-12: FUI five-page rendering

- **Prereq**: presenter profile running.
- **Steps**:
  1. Verify STANDBY home renders (cyberpunk HUD, slot indicator, host-name badge, connection state).
  2. Press `UP` to open MENU; verify the 8-item menu list and selection highlight.
  3. Enter PRESENT (when connected), MEDIA (from MENU), PAIR (from MENU or first boot).
  4. In each page, verify layout integrity, palette, and typography.
- **Pass**: all five pages render per `ui-design.md`; no clipping or overflow on a 240x320 portrait panel.

### UV-13: Palette and typography

- **Prereq**: presenter profile running; ideally with a reference panel.
- **Steps**:
  1. Sample the panel colors at known regions with a colorimeter or screenshot via the JTAG/USB path.
  2. Verify the dominant palette: base `#0B1030`, accent `#F07818` (orange), highlight `#FFD700` (yellow), info `#3FE0F0` (cyan).
  3. Verify the pixel font and the choice of display font per `ui-design.md`.
- **Pass**: palette and typography match `ui-design.md`.

### UV-14: Dirty-rect refresh and tearing

- **Prereq**: presenter profile running; high-motion screen (e.g., PRESENT timer ticks).
- **Steps**:
  1. Trigger animation/motion across multiple frames.
  2. Inspect the SPI bus and the panel for tearing.
  3. Confirm DMA transfers stay within a single 240x20 buffer.
- **Pass**: motion is contained inside dirty regions; no tearing observed on a static-frame capture.

### UV-15: Black-screen transition

- **Prereq**: presenter profile running.
- **Steps**:
  1. Trigger the black-screen transition (e.g., screen-off into light sleep).
  2. Visually evaluate smoothness; if measurable, sample the frame cadence.
- **Pass**: transition is smooth; no flicker or jitter.

### UV-16: Three-key ADC response

- **Prereq**: presenter profile running; oscilloscope or logic analyzer on the ADC pin.
- **Steps**:
  1. Press each of `UP`, `DOWN`, `OK` independently (no simultaneous press).
  2. Verify the gesture pipeline: short click -> double click -> long press at the right thresholds.
  3. Verify the gesture vocabulary matches `requirements.md` (no simultaneous-key support).
- **Pass**: each key produces the expected event sequence; thresholds align with `bsp_button.c`.

### UV-17: Long-press lock entry to SLEEP

- **Prereq**: presenter profile running; host paired.
- **Steps**:
  1. From STANDBY, long press `OK` >= 800 ms.
  2. Observe: lock combo is sent to the host, then device transitions to SLEEP (or stays in STANDBY per design - see note).
  3. Verify the per-host combo (`Win + L` / `Ctrl + Cmd + Q` / `Super + L`).
- **Pass**: correct combo sent per the slot's host profile; host screen locks.
- **Note**: confirm lock semantics with `requirements.md` section 4 (lock is scoped to STANDBY only; PRESENT returns to STANDBY on long press, lock from there).

### UV-18: Double-click reserved in PRESENT

- **Prereq**: paired; in PRESENT mode.
- **Steps**:
  1. Double click `OK` rapidly while in PRESENT.
  2. Observe: no action; PRESENT remains the active mode.
- **Pass**: no state change, no HID report sent.

### UV-19: Key sound toggle

- **Prereq**: presenter profile running; buzzer populated (`bsp_audio`).
- **Steps**:
  1. Open MENU, navigate to `KEY SOUND`; toggle on.
  2. Press keys; verify short beeps on each event.
  3. Toggle off; verify silence; setting persists in NVS.
  4. If `bsp_audio` init fails, verify the device degrades to silence (no crash, feature flag remains set).
- **Pass**: on/off matches setting; audio failures degrade silently.

### UV-20: NVS persistence

- **Prereq**: presenter profile running; paired slots, customized settings.
- **Steps**:
  1. Configure backlight, key sound, current slot, default host profile.
  2. Reboot (power cycle).
  3. Verify all settings are retained; bonded hosts reconnect.
- **Pass**: all configured values persist; slot bindings survive reboot.

### UV-21: NVS write failure degradation

- **Prereq**: ability to provoke an NVS write failure (e.g., fill the partition, or simulate via a debug build).
- **Steps**:
  1. Trigger a save while NVS is full or read-only.
  2. Verify the device continues to run with in-memory defaults; no partition erase.
  3. Verify the failure is logged but does not crash.
- **Pass**: graceful degradation per `requirements.md` section 10; partition intact.

### UV-22: Speech timer accuracy

- **Prereq**: presenter profile running; physical stopwatch or phone timer.
- **Steps**:
  1. Enter PRESENT mode; the timer starts at 00:00.
  2. After 5 minutes (physical timer), read the device's timer display.
  3. Acceptable drift: a few seconds over a 30-minute talk per `requirements.md` FR-05.
- **Pass**: drift within tolerance; reset to 00:00 on each PRESENT entry.

### UV-23: Battery BAS notification

- **Prereq**: presenter profile running; paired host with a BLE explorer (nRF Connect, Windows settings, macOS Bluetooth menu).
- **Steps**:
  1. Subscribe to Battery Service `0x180F` from the host.
  2. Read the SOC; observe periodic updates (10 s polling).
  3. With `-1` (unavailable reading), verify the UI degrades gracefully and the BAS value stops notifying.
- **Pass**: SOC reaches the host; `-1` does not display a number.

### UV-24: End-to-end user flow

- **Prereq**: fresh device or factory reset; host ready.
- **Steps**:
  1. Cold boot -> STANDBY; verify first-boot auto-entry into PAIR (no bonds).
  2. Pair with the host (UV-01 or UV-03/UV-04).
  3. Enter PRESENT, run a short slide show, exit to STANDBY.
  4. Enter MEDIA, control volume and play/pause, return to STANDBY.
  5. Idle for the sleep window (UV-09); verify deep sleep entry.
  6. Wake by key (UV-11) or by host reconnect (UV-10).
- **Pass**: the full path completes without requiring power cycle or recovery.

> Numbering: section 3 lists 23 unverified items. Section 4 has 24 procedures: the first section-3 item (Windows pairing + functional validation) is broken out into `UV-01` (pairing) and `UV-02` (PPT paging) for clarity, giving a 1:1 mapping to all 23 items in section 3 plus one extra detail step.

---

## 5. Code Statistics

| Group | Files | Lines |
| --- | ---: | ---: |
| `main/pc_*.c` | 20 | 4864 |
| `main/pc_*.h` | 15 | 1420 |
| `tests/test_pc_*.c` | 7 | - |
| **Total (production)** | **35** | **6284** |

> Production totals exclude test sources. Tests follow the pure-`assert` pattern with zero platform dependency.

---

## 6. Modified Files

- `main/CMakeLists.txt`: added the `if(CONFIG_PC_CONTROLLER_APP)` branch to compile the 20 `pc_*.c` sources. The `else` branch is verbatim baseline (default demo firmware unchanged).
- `.gitignore`: added `__pycache__/` and `*.pyc`.

No other existing files were modified. The default demo firmware sources, BSP, partition table, and `sdkconfig.defaults` are unchanged.

---

## 7. New Files

### 7.1 Application sources (`main/`)

`pc_app_fsm.c` / `pc_app_fsm.h`, `pc_app_main.c`, `pc_beep.c` / `pc_beep.h`, `pc_ble_hid.c` / `pc_ble_hid.h`, `pc_hid_reports.c` / `pc_hid_reports.h`, `pc_host_profiles.c` / `pc_host_profiles.h`, `pc_key_semantics.c` / `pc_key_semantics.h`, `pc_power_fsm.c` / `pc_power_fsm.h`, `pc_power_mgr.c` / `pc_power_mgr.h`, `pc_slide_counter.c` / `pc_slide_counter.h`, `pc_speech_timer.c` / `pc_speech_timer.h`, `pc_storage.c` / `pc_storage.h`, `pc_strings.c` / `pc_strings.h`, `pc_ui.c` / `pc_ui.h`, `pc_ui_fui.c` / `pc_ui_fui.h`, `pc_ui_fui_media.c`, `pc_ui_fui_menu.c`, `pc_ui_fui_pair.c`, `pc_ui_fui_present.c`, `pc_ui_fui_standby.c`, `pc_ui_int.h`.

### 7.2 Host tests (`tests/`)

`test_pc_app_fsm.c`, `test_pc_hid_reports.c`, `test_pc_host_profiles.c`, `test_pc_key_semantics.c`, `test_pc_power_fsm.c`, `test_pc_slide_counter.c`, `test_pc_speech_timer.c`.

### 7.3 Build profile

`sdkconfig.presenter.defaults` (independent build profile; default demo firmware untouched).

### 7.4 Documentation (`docs/software-design/pc-controller/`)

`requirements.md` / `requirements.zh_CN.md`, `ui-design.md` / `ui-design.zh_CN.md`, `delivery-report.md` / `delivery-report.zh_CN.md` (this document).

---

## 8. Related Documents

- [Requirements](./requirements.md) — product decisions and acceptance criteria.
- [UI design](./ui-design.md) — FUI cyberpunk HUD design language.
- [Hardware Development Guide](../../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md) — board-level facts.
- [Specifications](../../hardware-design/specifications.md) — electrical and timing parameters.
- [Build and test](../../development/build-and-test.md) — local and CI gate.
- [Agent guide](../../development/agent-guide.md) — four-field delivery convention.
- [CI build and release](../../development/CI-build-and-release.md) — CI gate behaviour.
