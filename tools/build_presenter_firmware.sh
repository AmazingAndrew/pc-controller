#!/bin/sh
# Build, merge and verify the PC Controller presenter firmware.
#
# This script must be invoked from the repository root inside the
# espressif/esp-idf-ci-action Docker image so that idf.py, python3 and
# the host project are all available.
#
# SDKCONFIG_DEFAULTS strategy:
#   ESP-IDF's kconfig treats SDKCONFIG_DEFAULTS as a single ;-separated
#   list of files (a CMake list), but inside esp-idf-ci-action's
#   `bash -c '...'` wrapper, layer-by-layer attempts to spell
#   `SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.presenter.defaults"`
#   have lost the semicolon or one of the two files in every iteration
#   (Runs #9 - #16). Run #9 and earlier proved that a single-file
#   SDKCONFIG_DEFAULTS=sdkconfig.presenter.link works as long as that
#   file is self-contained; sdkconfig.presenter.defaults already is (see
#   its own header comment), so the symlink approach removes the quoting
#   surface entirely and CMake processes a single file name.

set -eu

BUILD_DIR=${BUILD_DIR:-/tmp/presenter-build}
MERGED_BIN="$BUILD_DIR/FoloToy-AI-Passport-full.bin"

# 0. Make sure sdkconfig.presenter.link is a symlink to the self-contained
#    presenter defaults file. `ln -sf` is idempotent: if the link already
#    exists and points at the right target, it is left alone; otherwise it
#    is recreated. This must happen before any idf.py call so that
#    SDKCONFIG_DEFAULTS=sdkconfig.presenter.link resolves correctly.
ln -sf sdkconfig.presenter.defaults sdkconfig.presenter.link

# Pass SDKCONFIG_DEFAULTS as both env var and -D CMake flag to be defensive
# against any future action version that strips inline env prefixes.
export SDKCONFIG_DEFAULTS=sdkconfig.presenter.link

echo "[build_presenter_firmware] BUILD_DIR=$BUILD_DIR"
echo "[build_presenter_firmware] SDKCONFIG_DEFAULTS=$SDKCONFIG_DEFAULTS"

# 1. set-target populates the build directory and the sdkconfig file from
#    the symlinked presenter defaults.
idf.py -B "$BUILD_DIR" \
    -D SDKCONFIG_DEFAULTS=sdkconfig.presenter.link \
    set-target esp32c3

# 2. Compile the application. Re-running on the same BUILD_DIR is a no-op
#    when nothing has changed, so the second invocation can rely on the
#    set-target output.
idf.py -B "$BUILD_DIR" build

# 3. Produce the merged 8 MB image that the host BLE mini-program
#    installs.
idf.py -B "$BUILD_DIR" merge-bin -o "$MERGED_BIN"

# 4. Verify the presenter profile and the merged firmware layout. The
#    Python script is intentionally verbose on stderr so any mismatch
#    shows up in the GitHub Actions log instead of disappearing into a
#    silent "Process completed with exit code 2" annotation.
python3 tools/verify_presenter_build.py "$BUILD_DIR"