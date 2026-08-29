<p align="right">
  <a href="requirements.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# PC Controller - Requirements Specification

> Applies to: AI PASSPORT board (ESP32-C3).
> Baseline: dev-plan v0.1 (2026-08-29) + confirmed product decisions (2026-08-30).

This document is the requirements specification for the "PC Controller" application: a BLE HID presentation remote that runs on the existing AI PASSPORT hardware without any board or partition change. The confirmed product baseline below is the single source of truth; where it differs from the firmware development plan (dev-plan v0.1, 2026-08-29, maintained outside this repository and therefore only quoted in text), the confirmed baseline wins.

## 1. Overview and Goals

The PC Controller turns the AI PASSPORT board into a Bluetooth presentation remote: slide paging, full-screen control, one-touch host lock, media control, and a presentation timer, wrapped in a full-screen FUI-style interface driven by three buttons.

Core functional baseline:

- BLE HID via NimBLE HOGP: HID Service `0x1812` + Device Information `0x180A` + Battery Service `0x180F`; two reports - a standard 6KRO keyboard report and a Consumer Page report.
- Slide paging: `UP` = next page, `DOWN` = previous page.
- Full-screen toggle: `OK` short press sends `F5` and `Esc` alternately.
- One-touch lock: directly triggerable in standby mode only (in media mode, an `OK` long press first returns to standby, then the lock can be triggered); `OK` long press >= 800 ms; per-host combos: Windows = `Win + L`, macOS = `Ctrl + Cmd + Q`, Linux = `Super + L`.
- Media control: volume +/- and play/pause on the Consumer Page.
- Presentation timer: reset to zero whenever PRESENT mode is entered.
- Three device slots with serial switching (constrained by `MAX_CONNECTIONS=1`).
- Pairing / re-pairing: menu entry, bond overwrite, slot clearing, and automatic entry into pairing on first boot when no bond exists; the user guide must remind users to remove (forget) the device on the host first before re-pairing.
- Key sound (globally mutable, default off).
- Backlight / sleep policy: dim after 15 s idle, screen off after 60 s, then light sleep and eventually deep sleep.

Relationship to the default demo firmware: the PC Controller is an **independent build profile application; the default demo firmware is unchanged**. Both profiles share the same `main/` sources, BSP, partition table, and repository tooling; the default profile's build artifacts remain byte-for-byte identical.

### Non-Goals

- The black-screen feature (dev-plan's `B` key on double click) is cancelled.
- `OK` double click is reserved: the `BSP_BTN_DOUBLE` event stays in the event vocabulary but no action is bound to it in any mode.
- Lock screen and volume control are disabled in PRESENT mode; in PRESENT, `OK` long press returns to standby.
- The page number is a **local estimate**: counting starts at 1 when full screen is entered and steps +/-1 per page. No total page count is shown and no "EST" marker is displayed.
- All on-screen text is English only; no CJK font is introduced in this version.

## 2. Scope and Applicability

- Delivery form: an independent build profile selected by `SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.presenter.defaults"`.
- The presenter profile differs from the default profile only in sdkconfig: Wi-Fi disabled (`CONFIG_ESP_WIFI_ENABLED=n`), NimBLE Security Manager enabled, and `CONFIG_BT_NIMBLE_NVS_PERSIST=y` (currently `n` in [sdkconfig.defaults](../../../sdkconfig.defaults)); `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1` is kept.
- The partition contract in [partitions.csv](../../../partitions.csv) is not touched: factory application <= 3 MB, `cardid` @ `0x356000`, `recovery` @ `0x700000`.
- ESP-IDF stays strictly at 5.5.3.
- The triple-role contract of GPIO0 (shared ADC sampling pin for the three buttons, 5-second hold at boot entering Recovery, ESP32-C3 boot strap) is not changed; long-press semantics are registered and effective at runtime only. Holding `UP` at power-on entering ROM download mode remains documented factory behavior.

## 3. Hardware and Platform Constraints

Hardware facts are maintained in the hardware documents and are not duplicated here:

- [AI Hardware Development Guide](../../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md)
- [Hardware specifications](../../hardware-design/specifications.md)
- [bsp_pins.h](../../../components/bsp/include/bsp_pins.h) - single source of truth for pins and hardware parameters.

Software-side constraints that shape this design:

- LVGL uses a single 240 x 20-line buffer (`double_buffer=false`); enlarging it is forbidden.
- The panel has no TE signal: large redraws have a tearing window, so animation must stay inside small dirty regions ("static whole screen, moving parts only").
- The LVGL memory pool stays at 24 KB.
- The three buttons share one ADC channel and cannot be pressed simultaneously; all interactions are single-key gestures.

## 4. Functional Requirements

| ID | Requirement | Acceptance criteria |
| --- | --- | --- |
| FR-01 | Slide paging: `UP` sends next page, `DOWN` sends previous page via the keyboard report | On each of Windows / macOS / Linux, one press advances or returns exactly one slide; empty report released after each press (no stuck key) |
| FR-02 | Full-screen toggle in PRESENT: `OK` short press sends `F5` first, then `Esc`, with the device remembering the toggle state | First `OK` short press enters full screen, second exits it, on all three OSes |
| FR-03 | One-touch lock: `OK` long press >= 800 ms sends the host-profile lock combo (Windows `Win + L`, macOS `Ctrl + Cmd + Q`, Linux `Super + L`); directly triggerable in standby mode only (in media mode, an `OK` long press first returns to standby), never in PRESENT; combo stored per slot in NVS | Host actually locks on each of the three OSes; wrong combo is never sent to a host with a different recorded profile |
| FR-04 | Media control in MEDIA mode: `UP`/`DOWN` = volume +/-, `OK` short press = play/pause, via Consumer Page report | Volume steps and play/pause work on all three OSes |
| FR-05 | Presentation timer: counts elapsed time at 1 Hz and is reset to zero every time PRESENT mode is entered | Re-entering PRESENT shows 00:00; drift over a 30-minute talk <= a few seconds |
| FR-06 | Three device slots, serial switching under `MAX_CONNECTIONS=1`: switch first gracefully disconnects the current connection (after the disconnect, the device enters the suspend state per the HOGP specification), then runs directed advertising toward the target slot | Switching among three bonded hosts succeeds; only one connection ever exists |
| FR-07 | Pairing: Passkey Entry (6-digit code shown on screen), `PAIRING` menu entry, and automatic entry into PAIR on first boot when no bond exists | Pairing completes on all three OSes; first boot with empty NVS lands in PAIR without user action |
| FR-08 | Re-pairing: `CLEAR SLOT` clears the selected slot; pairing again overwrites the previous bond; the user guide section states that the host must remove (forget) the device before re-pairing | After clearing and re-pairing, the old bond is unusable and the new one works; user guide contains the host-side removal reminder |
| FR-09 | Key sound: short beep on key events, globally mutable, default off | Sound on/off matches the setting; audio failure degrades to silence |
| FR-10 | Backlight and sleep policy: backlight dims one step after 15 s idle, screen off after 60 s, then light sleep; deep sleep reachable (idle timeout or user action) | Measured current: presenting 15-35 mA; standby 0.2-1 mA; deep sleep 20-40 uA (520 mAh battery) |
| FR-11 | Battery display: SOC shown in the UI and exposed over Battery Service `0x180F` | Host (Windows settings / macOS Bluetooth menu) shows the remote's battery; reading of `-1` degrades gracefully instead of drawing a number |
| FR-12 | Page counter (local estimate): starts at 1 when full screen is entered, +/-1 per page; no total count, no "EST" marker | Counter follows paging exactly; after re-entering full screen it restarts from 1 |

## 5. Inputs and Outputs

Inputs:

- Buttons: `bsp_btn_t` (`BSP_BTN_UP` / `BSP_BTN_DOWN` / `BSP_BTN_OK`) x `bsp_btn_ev_t` (`BSP_BTN_PRESS` / `BSP_BTN_CLICK` / `BSP_BTN_DOUBLE` / `BSP_BTN_LONG`) delivered by the button callback, which runs in the button component's timer task context (see [bsp_button.h](../../../components/bsp/include/bsp_button.h)).
- Battery: integer SOC from `bsp_battery_soc()` (`-1` = unavailable).
- BLE: GAP/GATT events - connect, disconnect, subscribe, passkey request, supervision timeout.
- NVS: configuration, slot metadata, NimBLE bonds read at startup.

Outputs:

- HID keyboard reports (8-bit modifiers + 6KRO) and Consumer Page reports, with empty reports to release keys.
- LVGL UI updates (always under `bsp_lvgl_lock()` from non-LVGL contexts).
- Backlight PWM level and display sleep/wake.
- Audio beeps (key sound, pairing success, lock, low battery).
- BLE advertising control (directed / general) and connection parameter updates.
- NVS writes (configuration, slot metadata, bonds).
- Power-state transitions (light sleep / deep sleep / wake).

## 6. State and Key Semantics Matrix

Five application modes: STANDBY, PRESENT, MEDIA, PAIR, SLEEP. STANDBY itself has two layers: the standby home screen and the menu page.

State transitions:

| From | Trigger | To |
| --- | --- | --- |
| STANDBY (home) | `UP` short press | MENU |
| STANDBY (home) | `OK` short press while connected | PRESENT |
| STANDBY (home) | `OK` short press while not connected | PAIR |
| STANDBY (menu) | Menu item `MEDIA MODE` confirmed | MEDIA |
| STANDBY (menu) | Menu item `PAIRING` confirmed | PAIR |
| MENU | `OK` long press | STANDBY (home) |
| PRESENT | `OK` long press | STANDBY |
| MEDIA | `OK` long press | STANDBY |
| PAIR | `OK` short press (cancel) | STANDBY |
| Any active mode | Power timeout | SLEEP |
| SLEEP | Any key | STANDBY (first key event consumed, no function) |

Full key semantics matrix:

| Mode | Button | Short press (CLICK) | Double press (DOUBLE) | Long press (>= 800 ms) |
| --- | --- | --- | --- | --- |
| STANDBY (home) | UP | Enter the menu page | reserved (no action) | - |
| STANDBY (home) | DOWN | Cycle the device slot (1 -> 2 -> 3 -> 1, same semantics as slot switching in PAIR) | reserved (no action) | - |
| STANDBY (home) | OK | Connected: enter PRESENT; not connected: enter PAIR | reserved (no action) | Lock screen |
| STANDBY (menu) | UP / DOWN | Menu navigation over the 8 items | reserved (no action) | - |
| STANDBY (menu) | OK | Enter / confirm the selected item | reserved (no action) | Return to the standby home screen |
| PRESENT | UP / DOWN | Next page / previous page | reserved (no action) | - |
| PRESENT | OK | Toggle full screen (`F5` / `Esc`) | reserved (no action) | Return to STANDBY |
| MEDIA | UP / DOWN | Volume + / volume - | reserved (no action) | - |
| MEDIA | OK | Play / pause | reserved (no action) | Return to STANDBY |
| PAIR | DOWN | Cycle slot selection (slot 1 / 2 / 3) | reserved (no action) | - |
| PAIR | OK | Cancel and return to STANDBY | reserved (no action) | - |
| SLEEP | any | Wake only (first event consumed) | - | - |

The STANDBY menu has exactly 8 items: `PAIRING`, `CLEAR SLOT`, `SLOT`, `HOST PROFILE`, `KEY SOUND`, `BACKLIGHT`, `MEDIA MODE`, `ABOUT`.

Lock availability: the lock feature is scoped to standby mode only (excluded from PRESENT by decision). It can be triggered directly only in standby; in media mode all three keys are fully occupied and no gesture is free, so the `OK` long press in MEDIA first returns to STANDBY, from which the lock can then be triggered.

Power sub-state machine:

```text
ACTIVE --(15 s idle)--> DIM --(60 s idle)--> SCREEN OFF --> LIGHT SLEEP --> DEEP SLEEP
   ^__________________________ any key wakes (first key consumed) __________________________|
```

### Deviations from dev-plan v0.1

All deviations below are confirmed product decisions and take priority over dev-plan v0.1:

1. Double-click semantics: dev-plan bound double click to language switch / black screen / next track; the confirmed baseline leaves `BSP_BTN_DOUBLE` unbound everywhere and cancels the black-screen feature.
2. English-only interface: dev-plan planned bilingual UI with a subset CJK font; the confirmed baseline ships English-only text and defers CJK (see section 11).
3. PRESENT mode restrictions: dev-plan allowed lock (long press) and media gestures inside PRESENT; the confirmed baseline disables lock and volume in PRESENT and reassigns `OK` long press to "return to STANDBY".
4. Page counter: dev-plan left the page source open; the confirmed baseline fixes it as a local estimate starting from 1 at full-screen entry, with no total count and no "EST" marker.

## 7. Concurrency and Tasks

Runtime invariants defined in [agent-guide.md](../../development/agent-guide.md) ("Runtime invariants" section) apply in full. Task layout:

- The LVGL port task is the only task that steps `lv_timer_handler`. Any other task touching `lv_*` objects must hold `bsp_lvgl_lock()`; the application renders exclusively through that lock.
- The button callback runs in the button component's timer task and only enqueues light events; no blocking or heavy work there.
- The NimBLE host task owns GAP/GATT processing; HID report submission is triggered from the application side through the NimBLE API.
- One application task consumes the unified event queue (key semantics events, BLE events, timer events) and drives the FSM and UI.
- `esp_timer` instances: two backlight timeouts (15 s dim, 60 s screen off), the presentation timer tick at 1 Hz, and battery polling at 10 s.
- Page exit order: stop tasks/timers that may access the UI first, then delete the screen and clear object pointers.

## 8. Persistence

24 KB NVS partition layout:

| Namespace | Content | Notes |
| --- | --- | --- |
| `pp_cfg` | Backlight level, key sound on/off, current slot, default host profile | Global configuration |
| `pp_slot0` .. `pp_slot2` | Bond address, host name, OS type, lock combo, last-used time | One record per slot |
| NimBLE default namespace | Bonding records | Enabled by `CONFIG_BT_NIMBLE_NVS_PERSIST=y` |

- Initialization follows the "prepare without erasing on failure" pattern already used by `demo_radio_nvs_prepare()` in [demo_radio.c](../../../main/demo_radio.c): an NVS init failure degrades to in-memory defaults and never triggers a partition erase.
- Capacity estimate: 3 slots x ~500 B + NimBLE bonding 3-6 KB < 24 KB partition, with comfortable headroom.

## 9. Memory Budget

- Wi-Fi is disabled in the presenter profile, releasing roughly 30-50 KB of RAM.
- The LVGL memory pool stays at 24 KB and the 240 x 20-line single buffer is unchanged (repository red line).
- All fonts and bitmaps live in Flash (memory-mapped at runtime); RAM cost is only the LVGL font descriptors.
- Acceptance is measured, not estimated: `esp_get_free_heap_size()` on real hardware after boot must report free heap >= 120 KB.
- Every new image, font, network stack addition, audio buffer, LVGL buffer, or task stack is evaluated against internal RAM; total free heap does not guarantee a large-enough contiguous block.

## 10. Failure Degradation

| Failure | Degradation |
| --- | --- |
| BLE disconnected (supervision timeout) | Directed advertising to the bonded host for 30 s, then general discoverable advertising for 2 min with decreasing duty cycle; any key press raises the advertising duty cycle immediately |
| Battery read returns `-1` | UI degrades gracefully (no number drawn); Battery Service stops notifying a value |
| Audio init/play failure | Key sound and all beeps degrade to silence; the feature flag stays on |
| Display init failure | Boot continues with logs; no UI-dependent function blocks the radio path |
| Deep-sleep wake by GPIO0 key not verified on this board | Two fallback paths: (a) stay in light sleep with periodic advertising windows; (b) rely on the independent hardware power button |

## 11. Extensibility

- `page_source` abstraction: the page counter is read through an interface whose default implementation is the local estimator. A future `companion_page_source` can receive the real page number over GATT with zero UI changes.
- The GATT table reserves one Vendor Service UUID slot for future companion features.
- The host-profile table (OS type -> lock combo) is data-driven and extensible to new OSes or custom Linux desktop combos.
- UI strings are index-based string tables; reserving a second table per index keeps the door open for Chinese localization later without restructuring the UI code.

## 12. Build Profile and Repository Contract

- The presenter profile is not yet gated by [tools/validate.sh](../../../tools/validate.sh); adding it to the CI gate requires a separate proposal.
- The default profile's build artifacts must remain byte-for-byte identical before and after this work.
- The partition verification contract enforced by [tools/verify_firmware.py](../../../tools/verify_firmware.py) (factory size, `cardid`, `recovery` positions) is unaffected.

## 13. Test Strategy and Acceptance

- Platform-independent logic - `key_semantics`, `app_fsm`, the presentation timer, and the page counter - is implemented without ESP-IDF/LVGL dependencies and covered by host tests, following the pure-assert pattern of [test_ui_pixel_math.c](../../../tests/test_ui_pixel_math.c).
- Real-device acceptance matrix across Windows, macOS, and Linux: pair -> paging -> full-screen toggle -> lock -> disconnect -> automatic reconnect, executed per OS; media control and three-slot switching included.
- Deep-sleep wake via GPIO0 key press is a mandatory test item (board behavior unverified, see section 15).
- Quantitative acceptance thresholds: free heap >= 120 KB after boot; presenting 15-35 mA; standby 0.2-1 mA; deep sleep 20-40 uA (520 mAh battery).
- Every delivery reports in the four-field format from [agent-guide.md](../../development/agent-guide.md): `Build: PASS / FAIL / NOT RUN`, `Host tests: ...`, `Device tests: ...`, `Unverified: ...`.

## 14. Compliance and Decision Record

ui_pixel compliance argument:

- The rule's origin is the "Runtime invariants" section of [agent-guide.md](../../development/agent-guide.md): the obligation to keep the `ui_pixel` theme system applies when modifying pages of the default demo application.
- This design is a parallel, independent build profile: the default application and `ui_pixel` receive zero modifications.
- Neither [tools/check_repo.py](../../../tools/check_repo.py) nor [tools/validate.sh](../../../tools/validate.sh) checks UI themes.
- Alternatives considered and rejected:
  - In-place replacement of the default demo with the controller UI: rejected, because it violates the ui_pixel preservation rule for the default application.
  - Pre-creating a `plays/` entry before the application exists: rejected, because the repository forbids creating skeleton entries without actual content.
  - Hybrid dual-theme build (default pixel theme + controller pages): rejected, because the user chose a full FUI identity for this profile.
- Any future decision to remove or replace `ui_pixel` is deferred to a separate proposal.

## 15. Open Issues

- Deep-sleep wake through the GPIO0 key path is unverified on this board; the fallback paths in section 10 apply until measured.
- macOS reconnect behavior (occasionally slow reconnect) needs real-device measurement with directed advertising.
- License verification of the pixel font candidate (OFL) is still pending before the font is committed to `assets/fonts/`.
