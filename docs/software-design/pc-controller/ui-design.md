<p align="right">
  <a href="ui-design.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# UI Design: PC Controller (FUI Cyberpunk HUD)

**Scope:** PC Controller application build profile on the AI PASSPORT board (ESP32-C3, ST7789 240×320 portrait, RGB565, single 240×20-line LVGL buffer, no TE signal).

**Baseline:** confirmed UI decisions (2026-08-30), visual mockup fui-v6.

Quantitative acceptance metrics are maintained in `requirements.md` (same directory, single source of truth). This document says *see requirements.md* where relevant and does not duplicate numbers.

## 1. Design Language

The entire PC Controller UI is a fictional-user-interface (FUI) cyberpunk HUD in the Nicolas Lopardo tradition: a deep navy field, glowing orange panels, fluorescent-yellow status text, cyan labels, a pixel HUD typeface, grid/scanline texture, corner brackets on every panel, and a CRT scanline feel.

Design rationale — this style is not only an aesthetic choice but a direct fit for the display hardware:

- The panel has **no TE signal** and runs from a **single 240×20-line buffer** (`components/bsp/src/bsp_display_lvgl.c`, `double_buffer = false`). Every frame therefore has a tearing window; tearing is only visible where content is changing.
- A dark background plus a few small bright elements minimizes the dirty area per frame, so the guiding discipline is **"static full screen, motion only in local spots"**.
- Deep background + thin bright lines + local animation is the cheapest possible workload for a single-buffer, no-TE pipeline. Bright-on-dark HUD graphics degrade gracefully; bright-on-white layouts would expose every tear line.

All on-screen text is English. The device has no CJK font and none will be added (see section 6).

## 2. Design Tokens

Tokens below are the single source of truth for colors and panel decoration.

| Token | Value | Role |
|---|---|---|
| `BG` | `#0B1030` | Full-screen background (deep navy) |
| `PANEL_BG` | `#0D1338` | Panel fill |
| `PANEL_GLOW` | `#F07818` | Panel border, glow, and primary accent (orange) |
| `STATUS` | `#FFD700` | Status text, key names, h1, corner brackets (fluorescent yellow) |
| `LABEL` | `#3FE0F0` | Panel labels, top-bar mode name, timer digits (cyan) |
| `TEXT` | `#EAF2FF` | Body text (white) |
| `FRAME` | `#2A2F55` | Outer frame, footer divider (blue-gray) |
| `LAMP_OFF` | `#1A2148` | Lamp segment off state |
| `OK_GREEN` | `#3FF08F` | Stable-link indication only — no other use |

Component rules:

- **Panel:** 2 px `PANEL_GLOW` border + outer glow + inner glow + corner brackets. Brackets are a diagonal pair (top-left and bottom-right), 2 px, `STATUS` color.
- **Top bar:** mode name on the left (`LABEL` with glow) + battery icon and percentage on the right (`STATUS`), value from `bsp_battery_soc()`.
- **Page title (h1):** `STATUS` color with a slight skew (≈ −3°); subtitle below it is small `FRAME`-tone text with wide letter-spacing.
- **Lamp bar (status indicator):** segments are 12×5 px. Lit segments come in two semantics: orange glow (`PANEL_GLOW`) and cyan glow (`LABEL`). Off segments use `LAMP_OFF`.
- **Footer key legend:** key name in `STATUS` + action in `TEXT`, always all-English, e.g. `OK FULLSCR`, `OK HOLD EXIT`, `OK PAIR`, `DOWN SLOT`. `HOLD` in the legend corresponds to the >= 800 ms long-press threshold defined in requirements.

## 3. Layout System

Portrait 240×320 px, reference zoning:

| Zone | Y range | Content |
|---|---|---|
| Top bar | 0–32 | Mode name (left), battery icon + percentage (right) |
| Content | 32–296 | h1, subtitle, panels stacked vertically |
| Footer | 296–320 | Key legend, separated by a 1 px `FRAME` divider |

Rules:

- Safe margin: **10 px** on all four sides; no text or panel edge may cross it.
- Panels stack top-down inside the content zone with equal vertical gaps; the visual mockup is drawn at 0.8× of the target panel (192×256), so mockup pixel values scale up by 1.25× when applied to the device.
- Reference sizes (mockup-derived unless stated): top-bar text ≈ 11 px, h1 ≈ 16 px, subtitle ≈ 8 px, panel label ≈ 8 px, footer legend ≈ 8 px. The 36 px timer and 16 px slide number in section 4 are normative device values.
- Battery widget never overlaps other top-bar content; if the reading is unavailable it degrades per section 8.

## 4. Page Specifications

Every page draws the full background texture (grid + scanlines) once on entry and then refreshes dirty regions only (section 5).

### 4.1 Standby (boot/home)

Elements:

- Top bar: `PC-CTRL` (left, cyan glow) + battery icon/percentage (right).
- h1 `STANDBY` (skewed yellow); subtitle e.g. `PRESENTATION CORE v1.0`.
- Panel 1 `HOST LINK`: label row with status word (`SEARCHING…` / `CONNECTED` / `DISCONNECTED`), large center status (20–24 px) showing `STARTING` / `PLEASE WAIT` during boot or the connected host name afterwards, and a lamp bar underneath.
- Panel 2 `PROFILE`: single label row, value `WINDOWS` / `MACOS` / `LINUX` (yellow, right side).
- Footer: `OK PAIR/PRESENT / UP MENU / DOWN SLOT`.
- Menu: a separate 8-item list page entered via `UP`; the selected item is highlighted with an orange fill and black text, `OK` short press enters/confirms the selected item, and `OK` long press returns to the standby home screen.

State variants: `SEARCHING…` (animated lamp), `CONNECTED` (host name shown, lamp steady), `DISCONNECTED` (status word only, lamp dimmed).

### 4.2 Present (core page)

Elements:

- Top bar: `PRESENT` + battery.
- h1 `PRESENT MODE`; subtitle `SLIDE CONTROL ACTIVE`.
- Panel 1 `SPEECH TIMER`: label row with `RUN` state on the right; **36 px cyan glowing digits** as the page's primary visual (normative size). Only the digit area is redrawn each second.
- Panel 2 `SLIDE`: **16 px orange glowing page number** (normative size). The number is a local estimate — the host never reports a total page count, so no `x/y` format is ever shown. Hint line: `▲ UP · ▼ DOWN`.
- Panel 3 `HOST LINK`: single label row, value `STABLE` (yellow) — this is the only place where `OK_GREEN` may instead indicate a stable link.
- Footer: only `OK FULLSCR` and `OK HOLD EXIT`. The double-click slot is reserved in the event vocabulary but has no bound action; the footer legend leaves it empty accordingly (aligned with the requirements Non-Goals).

State variants: on link loss, panel 3 switches to red `LOST` text with a blink cue (alarm red is a state-local color, outside the baseline token set). Lock and volume controls are disabled in this mode and never appear in the legend.

### 4.3 Pairing

Elements:

- Top bar: `PAIRING` + battery.
- h1 `BLE HID`; subtitle `DISCOVERABLE · SLOT n/3`.
- Panel 1 `HOST LINK`: label row value `OPEN`; center word `SEARCHING` (16 px); sub line `SELECT "PC-CTRL" ON HOST`; lamp bar blinking with **alternating orange/cyan** segments.
- Panel 2 `BOUND SLOTS`: label row, value `x/3`.
- Footer: `OK CANCEL / DOWN SLOT`.

State variants: advertising (blinking lamp) vs. connection incoming (lamp settles to the standby style).

### 4.4 Action Feedback / Media

Two sub-states share this page:

- **Lock feedback:** panel with label row `COMMAND` / `WIN+L`, large center word `LOCKED`, sub line `PROFILE: WINDOWS`. The `COMMAND` row shows the lock combo of the current host profile (`WIN+L` / `CTRL+CMD+Q` / `SUPER+L`) and the `PROFILE` row shows the profile name (`WINDOWS` / `MACOS` / `LINUX`). Shown for **1.5 s**, then the page returns automatically to standby.
- **Media mode:** panel with label row `MEDIA MODE` / `VOL nn` (volume value, two digits). Footer: `UP VOL+ / DOWN VOL- / OK PLAY`; an OK long-press returns to standby.

The lock can be triggered only from standby mode (the lock feedback page is likewise shown only after a standby-triggered lock) — never from Present or media mode.

## 5. Rendering Discipline

Hard constraints from the BSP: single 240×20-line DMA buffer, `double_buffer = false`, no TE. A full screen is 240×320×2 B = 150 KB, flushed in **16 batches** of 240×20 per frame; a theoretical full redraw tops out near 30 fps on the 40 MHz SPI before CPU rendering lowers it further.

Rules:

1. **One-shot background:** on page entry, draw the grid/scanline texture once (16 batches × 240×20). All subsequent frames touch dirty regions only. Frame-rate target for animated elements: **20–30 fps**.
2. **Dirty-area budget:**

| Element | Dirty area | Notes |
|---|---|---|
| Scanline bright band | 240×2 px (960 B at RGB565), < 1 KB/frame | `lv_anim` on y axis, 2–4 s period |
| Timer digits | 1–4 KB/s | only the digit rectangle redraws once per second |
| Slide number | updated on page change only | no periodic refresh |
| Link / battery | event-driven only | no polling redraws |

3. **Prohibited:** per-frame full-screen animation; switching to double buffering; sustained high-contrast motion that splits the screen into two halves (worst case for tearing without TE); any permanent full-screen blink.
4. **Page transition:** a one-shot blackout — clear the screen and lower the backlight, then load and draw the new page, then restore the backlight. No continuous transition animation.

Further budget reasoning comes from the firmware development plan (FUI rendering budget section, project-workspace document outside this repository; quoted in text only, no link). Quantitative acceptance values: see requirements.md.

## 6. Font Assets

- Typeface: a HUD pixel font; reference candidate is Press Start 2P (OFL license **pending verification** before adoption; substitute with an equivalently licensed pixel font if verification fails).
- Subsetting: `lv_font_conv`, roughly **70 English glyphs** actually used by the UI strings, 16 px, 4 bpp.
- Timer digit subset: a separate **36 px digit subset** for the speech timer (only the 11 glyphs 0–9 plus colon), 4 bpp, estimated **<= 2 KB Flash**.
- Budget: **6–10 KB Flash for the 16 px subset + <= 2 KB Flash for the 36 px digit subset**; stored memory-mapped in Flash, so RAM cost is ≈ 0 (this board has no PSRAM).
- Placement: font binary plus generated C array go into `assets/fonts/` and are registered following the rules in [assets/fonts/README.md](../../../assets/fonts/README.md) (destination, naming, integration method, source/license).
- **No CJK font is introduced.** All screen text is English; omitting a CJK subset saves the 30–45 KB Flash a Chinese bitmap subset would cost and avoids any glyph-missing fallback on screen.

## 7. Motion and Feedback

- **Key sound:** every accepted key event plays a short blip through the audio path (`components/bsp` audio); rejected/invalid presses use a distinct low tone. Acceptance levels: see requirements.md.
- **Scanline animation:** one 240×2 px bright band drifting slowly downward, `lv_anim` y-axis loop with a 2–4 s period; it only dirties the narrow strip it crosses.
- **Lamp rhythm:** standby searching = progressive fill; pairing = orange/cyan alternating blink; connected/stable = steady. Blink cadence stays slow (≥ 500 ms half-period) so the dirty area remains tiny.
- **Action feedback duration:** lock/media feedback panels are shown for **1.5 s**, then the previous page is restored via the one-shot blackout transition.
- **Lost-link blink:** the red `LOST` state blinks at a slow cadence; blink redraws touch only the label rectangle.

## 8. Degradation

- **Battery reading `-1`:** `bsp_battery_soc()` returning `-1` means unavailable. The top bar draws the battery icon outline only, without any percentage number — never draw `-1` or `0%`.
- **Link loss:** Present page switches panel 3 to red `LOST` with blink (section 4.2); Standby shows `DISCONNECTED`. All slide/timer visuals stay frozen at their last values.
- **Display init failure:** if display initialization fails, the application must not block or crash; skip all UI work, keep button/BLE functions alive where possible, and surface the failure through logs. No UI-level retry loop may hammer the SPI bus.
- **LVGL lock discipline:** any non-LVGL context touching LVGL objects holds `bsp_lvgl_lock()` / `bsp_lvgl_unlock()` (see `docs/development/agent-guide.md` runtime invariants).

## 9. Reference Mockup

Two visual mockups define the look of this baseline; both are **temporary files in the project workspace, outside this repository**, and must not be copied into the repo:

- `fui-v6-present.html` — Present mode final mockup (the fui-v6 baseline named above).
- `fui-lopardo-v3.html` — final mockups for Standby, Pairing, and Action Feedback / Media pages.

They are rendered at 0.8× panel scale (192×256 px) for review convenience and serve as the visual source of truth only; when this document and a mockup conflict, this document and the token table win. Related quantitative requirements live in `requirements.md`.

Related documents: [software design index](../README.md).
