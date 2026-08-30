<p align="right">
  <a href="ci-pitfalls.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# CI Build Pitfalls: From Dual sdkconfig to Upstream Simplicity

Captured after a long sequence of CI failures on the PC Controller (BLE HID PPT
remote) firmware branch. This entry records the eight concrete pitfalls that
burned hours of debugging time, so the next contributor who needs an independent
build profile does not have to rediscover them.

## Context and the hard limit

The PC Controller feature is a fork-local build target: a BLE HID presenter that
sends keyboard / consumer-control / vendor HID reports to a paired PC to drive
PowerPoint or Keynote slide advance, blank-screen, volume, and a one-shot speech
timer. It must coexist with the upstream demo firmware without overwriting it.

Two facts constrain the CI design from the start:

- The upstream `FoloToy/ai-passport` builds one firmware per push, driven by a
  single `sdkconfig.defaults` and the `validate.sh --firmware` one-liner. Its
  CI pattern is documented in `docs/development/CI-build-and-release.md` and
  `docs/development/CI-validation.md`.
- The PC Controller needs an independent build profile so the default demo
  build is not disturbed.

We chose to invent a **dual-sdkconfig architecture**
(`sdkconfig.defaults` + `sdkconfig.presenter.defaults`) without first reading
the upstream docs end-to-end. That choice cascaded into the eight pitfalls
below, in chronological order over several hours and **8+ consecutive CI
failures** before we stopped, returned to upstream, and simplified.

## Pitfall list (chronological)

Each pitfall is recorded as it was hit, with symptom, root cause, fix, and the
single reusable takeaway.

### 1. Windows Git strips executable permission

- **Symptom**: `./tools/install-actionlint.sh: Permission denied` (exit 126)
  on the very first CI run after committing a shell script.
- **Root cause**: Git on Windows does not preserve the executable bit when
  committing shell scripts. Files that are `100755` in the upstream checkout
  become `100644` after a Windows-side clone, add, and push.
- **Fix**: `git update-index --chmod=+x tools/install-actionlint.sh tools/validate.sh`
  to restore the bit, then commit the mode change.
- **Lesson**: Always set executable permissions explicitly after cloning on
  Windows. Treat `100644` shell scripts as a defect, not a default.

### 2. SDKCONFIG_DEFAULTS needs CMake semicolon list

- **Symptom**:
  `SDKCONFIG_DEFAULTS '.../sdkconfig.defaults sdkconfig.presenter.defaults' does not exist`.
- **Root cause**: CMake lists use semicolons, not spaces, as separators. A
  space-separated value is parsed as one long path, which of course does not
  exist.
- **Fix**: Use `;` instead of space between file names:
  `-D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.presenter.defaults'`.
- **Lesson**: CMake list format is counterintuitive — spaces create a single
  path, semicolons create a list. YAML must single-quote the whole value so the
  shell does not consume the `;`.

### 3. esp-idf-ci-action wraps `command` in `bash -c '...'`

- **Symptom**: `command not found` (exit 127) when the `command:` input
  contains single quotes (for example, to wrap a value with spaces).
- **Root cause**: `espressif/esp-idf-ci-action` wraps the `command:` input in
  `bash -c '...'`. Any single quote in the command terminates the outer quoting
  early, so the action receives a malformed shell line.
- **Fix**: Avoid single quotes entirely in `command:`. If a value has spaces,
  use a symlink to a path without spaces rather than quoting.
- **Lesson**: Never use single quotes in `command:` for
  `espressif/esp-idf-ci-action`. The same applies to any `bash -c`-style
  wrapping action.

### 4. YAML multiline backslash continuation is broken

- **Symptom**: Command fragments are treated as separate executables
  (`sdkconfig.presenter.defaults: command not found`, or
  `set-target: command not found`).
- **Root cause**: `espressif/esp-idf-ci-action` does not handle YAML block
  scalar + backslash line continuation correctly. The action appears to read
  each line as its own command, so `command: |` with `\` continuations is
  split into fragments.
- **Fix**: Use single-line commands only. Join with `&&` where multiple steps
  are needed.
- **Lesson**: Keep `command:` values on a single line. Do not rely on shell
  line continuation inside the action input.

### 5. Semicolons interpreted as shell command separators

- **Symptom**: `sdkconfig.presenter.defaults: command not found` even after
  switching to the semicolon form required by CMake.
- **Root cause**: Even with semicolons for CMake, the action's `bash -c` layer
  interprets `;` as a shell command separator. The two semantics collide: CMake
  wants `;` inside the `-D` value, the shell wants `;` to end the command.
- **Fix**: Avoid semicolons entirely. Either use a symlink to make a
  single-file path, or merge the configs into one file.
- **Lesson**: The action's command parsing is hostile to complex arguments. Any
  shell metacharacter inside the `command:` is a trap.

### 6. Symlinks in Docker-mounted volumes

- **Symptom**: Configure phase passes, but the actual `idf.py build` step
  fails to find the linked file (`No such file or directory`).
- **Root cause**: Docker volume mounts combined with symlink resolution behave
  inconsistently across platforms and tmpfs configurations. A symlink that works
  on the host may dangle inside the CI container.
- **Fix**: Use `cp` instead of `ln -s`, or merge the two configs into a single
  file so no symlink is needed.
- **Lesson**: Do not rely on symlinks in CI Docker environments. The simplest
  fix is to remove the indirection entirely.

### 7. ESP-IDF v5.5.3 / GCC 14 / LVGL v9 API drift

- **Symptom**: Multiple compile errors during the ninja phase:
  implicit-function-declaration (NVS), too many arguments (NimBLE),
  undeclared identifiers (LVGL), and `-Werror=enum-compare` (cross-enum
  comparison).
- **Root cause**: Three independent toolchain uplifts:
  - NVS API rename: `nvs_delete_key` → `nvs_erase_key`.
  - NimBLE signature tightening: `ble_hs_mbuf_to_flat` now takes 4 args,
    `BLE_ERR_REM_USER_CONN_TERMINATED` renamed to `BLE_ERR_REM_USER_CONN_TERM`,
    and the `appearance_is_complete` field renamed to `appearance_is_present`.
  - LVGL v9 removed `lv_obj_set_style_text_shadow_*`; replaced with the box
    shadow API.
- **Fix**: Update the source files to match the current API. The 7 files
  touched were `pc_storage.c`, `pc_app_main.c`, `pc_ble_hid.c`, and the LVGL
  v9 shadow migration across `pc_ui_fui.c`, `pc_ui_fui_media.c`,
  `pc_ui_fui_present.c`, plus `sdkconfig.defaults` for the font flag.
- **Lesson**: Pin ESP-IDF version and test against it locally before pushing to
  CI. A local `idf.py build` would have surfaced all of these in seconds
  instead of minutes-per-CI-cycle.

### 8. Font config in the wrong sdkconfig file

- **Symptom**: `lv_font_unscii_8 undeclared` (linker error, even though the
  compile passed).
- **Root cause**: `CONFIG_LV_FONT_UNSCII_8=y` was placed in
  `sdkconfig.defaults`, but the CI build only loaded
  `sdkconfig.presenter.defaults`. The flag never reached the build.
- **Fix**: Move the font flag into `sdkconfig.presenter.defaults` (or into
  whichever file the CI actually loads — there should be exactly one).
- **Lesson**: Know exactly which config files your CI loads. There is no value
  in splitting defaults if the CI never sees the second file.

## Root cause analysis

Distilled, the eight failures break down into three categories by weight:

- **70% methodology error**: We invented a dual-sdkconfig architecture without
  reading the upstream `CI-build-and-release.md` first. The upstream uses a
  single `sdkconfig.defaults` plus `validate.sh --firmware`. Our complexity
  was unnecessary.
- **20% toolchain version**: ESP-IDF v5.5.3 / GCC 14 / LVGL v9 API changes are
  legitimate and expected. A local `idf.py build` would have caught all of
  them before any push.
- **10% architecture mismatch**: A dual-sdkconfig design *could* work, but its
  implementation conflicted with kconfig behavior, Docker tmpfs isolation,
  symlink handling, and the `espressif/esp-idf-ci-action` argument-parsing
  semantics. The architecture itself was fighting the toolchain.

## The correct approach

After all eight pitfalls, the right structure is the upstream structure:

1. Use a single `sdkconfig.defaults` with all needed configs (including the
   presenter-only flags).
2. Let `validate.sh --firmware` handle the build — it already knows the
   correct target, partition table, and merge steps.
3. CI workflow: one `espressif/esp-idf-ci-action` step with
   `command: idf.py build`. No `set-target`, no `SDKCONFIG_DEFAULTS`, no
   custom merging, no symlinks.
4. If the presenter build must stay independent of the demo build, drive it
   with a `validate.sh --firmware --profile presenter` style flag, not with a
   second sdkconfig file.

## Reusable takeaways

These are the rules to keep for the next contributor, ordered by impact:

1. **Read upstream docs first.** `docs/development/CI-build-and-release.md`
   and `docs/development/CI-validation.md` describe the exact CI pattern this
   fork should follow. Diverging without a documented reason costs hours.
2. **Match upstream patterns.** Forks should minimize architectural divergence
   from upstream. Every divergence is a maintenance tax and a CI trap.
3. **Test locally before CI.** Running `validate.sh --firmware` (or the
   matching local entry point) before every push catches ~80% of CI failures
   in seconds. The remaining 20% are infrastructure-only and need a real CI
   run.
4. **Avoid clever workarounds.** Symlinks, environment-variable injection,
   multi-file SDKCONFIG_DEFAULTS, and quote escaping are all fragile in CI
   environments. If the upstream pattern does not need them, neither do you.
5. **One commit per concern.** Separate API fixes from CI config changes from
   workflow fixes. Mixed commits make bisection and rollback painful.
6. **Pin toolchain versions.** Record the ESP-IDF, GCC, and LVGL versions in
   CI (the `IDF_VERSION` action input) and rebuild locally against the same
   version before pushing.

## Route

This is general fork-dev experience on top of `FoloToy/ai-passport`. It is
proposed back to the upstream as a documentation PR so other contributors
do not repeat the same eight pitfalls.
