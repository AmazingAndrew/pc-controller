<p align="right">
  <a href="ble-pitfalls.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# BLE / HID Pitfall Notes

This page records known pitfalls observed while shipping the BLE HID
presenter remote on the FoloToy AI Passport. Symptoms are written as
seen on real hardware against the current PC Controller codebase; root
cause analysis references `main/pc_storage.h`, `main/pc_ble_hid.c`, and
`components/esp_ble_hid` (managed NimBLE host).

The notes are project-local: they complement but do not replace the
upstream NimBLE Security Manager requirements documented in
`docs/software-design/pc-controller/requirements.md`.

## 1. Slot record drops the address type

### Symptom
After pairing with a host and rebooting, the device enters directed
advertising using `BLE_ADDR_PUBLIC`, but some hosts (notably macOS
laptops that have re-randomized their identity) advertise a random
address. The first directed advertisement window fails to match, so the
reconnect step silently downgrades to general advertising and falls
back to the 2-minute window instead of the 30-second directed window.

### Root cause
`pc_slot_t` only persists the 6-byte peer address (`addr[6]`). The
address type (`BLE_ADDR_TYPE_PUBLIC` / `BLE_ADDR_TYPE_RANDOM` vs the
NimBLE privacy variants) is captured at pairing time but discarded
before NVS write, so post-reboot the directed advertiser always guesses
`BLE_ADDR_PUBLIC`.

### Fix
Extend `pc_slot_t` with an `addr_type` byte (default to
`BLE_ADDR_PUBLIC` when missing, to keep NVS-compat with older slots),
thread it through `pc_slot_load` / `pc_slot_save`, and pass it into
`pc_ble_hid_start_adv_directed()` so the directed advertiser uses the
real address type recorded at pairing time.

### Lessons
- Pairing-supplied identity attributes must be persisted holistically;
  saving only the bytes you can see on the wire (the 6-byte address)
  is not enough.
- Whenever a NimBLE API takes an `addr_type`, it must come from a
  trustable source — usually the original gap event — and not from a
  constant.

## 2. `pc_slot_clear` does not remove the bond

### Symptom
After clearing a slot from the menu (item 2 = "Slot Clear"), the
deleted slot still re-appears as "previously bonded" on the next host
boot. The host may attempt to auto-reconnect to the old slot using a
cached long-term key, which can pull the device out of standby with no
user action.

### Root cause
`pc_slot_clear()` only erases the slot namespace keys in NVS. NimBLE
stores its own bond database separately (typically in its own
`ble_hs` / `ble_store` flash region), so the host's long-term key,
peer IRK / LTK, and our local CSRK are all still on flash after
"slot clear".

### Fix
Augment `pc_slot_clear()` to invoke `ble_gap_unpair()` (or
`nimble_port_clear_bond_db()`) for the cleared peer address, before
the NVS erase or immediately after. Log a degraded warning if the
unpair step fails, but still complete the NVS erase so the UI does
not get stuck on a slot that no longer exists.

### Lessons
- "Clear slot" is a stronger operation than "forget metadata"; it must
  also break the trust relationship that NimBLE maintains alongside
  application storage.
- Surface the failure mode in logs even when UI feedback cannot be
  given (clear is intentionally silent in the menu).

## 3. Stuck-key from a dropped release report

### Symptom
After long PowerPoint sessions, occasionally the host sees a key as
held down indefinitely — the device keeps emitting "press" of e.g.
Page Down without ever sending the matching "release". The host then
auto-repeats or, in full-screen mode, flies past the slide deck.

### Root cause
`pc_ble_hid_send_keyboard()` ships a press report synchronously and
relies on the same code path to ship the matching release (zeroed
modifiers / zeroed keys). If the second `ble_gap_conn` / TX call fails
(e.g. transient radio contention right after the press), the device
keeps the press logically open. There is no compensation / retry on
the release side.

### Fix
Add a single retry on the release path: if the second NimBLE send
fails, queue a fresh release attempt for the next app task iteration
using the same `(mods, key)` pair. As a last resort, kick a periodic
"force-zero" timer (e.g. 500 ms) that issues a zeroed release report
if any press has been outstanding beyond a budget.

### Lessons
- A HID keyboard report is two-frame: press and release. The release
  frame must be just as reliable as the press; "send best-effort" is
  acceptable for the press but not for the release.
- Periodic watchdog / sweep logic is a reasonable fallback for
  key-stuck scenarios because the cost is one zeroed report per
  budget window — negligible against the cost of an out-of-control
  presentation.
