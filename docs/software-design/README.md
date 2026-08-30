<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Software Design

This directory holds application-, component-, and system-level designs: module boundaries, interfaces, state machines, resources, persistence, concurrency, failure degradation, and test strategy.

Use one descriptive file or subdirectory per topic. State scope and applicable version or commit. Reference stable hardware documents instead of copying pin or board facts.

The authoritative AI entry point is [AGENTS.md](../../AGENTS.md); collaboration rules are under `docs/contribution/`, engineering rules under `docs/development/`, and fork workflow in `docs/fork-guide.md`.

## Existing document index

- [AGENTS.md](../../AGENTS.md): the authoritative AI specification entry and index for the repository.
- [PC Controller requirements](pc-controller/requirements.md): requirements specification of the PC Controller BLE HID presentation remote running on the AI PASSPORT board.
- [PC Controller UI design](pc-controller/ui-design.md): UI design of the PC Controller application, covering the FUI cyberpunk HUD design language, layout, states, and rendering discipline.
- [PC Controller delivery report](pc-controller/delivery-report.md): four-field firmware delivery report (Build / Host tests / Device tests / Unverified) for the `feature/pc-controller` branch, with per-item on-board acceptance methods.
