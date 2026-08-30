#!/bin/sh
# Build, merge and verify the PC Controller presenter firmware.
#
# Run #15 failed in 1m29s when this whole pipeline was expressed as a long
# YAML string with several layers of escaping. Hard-coding it as a shell
# script keeps the SDKCONFIG_DEFAULTS layering, the idf.py commands, and
# the verify_presenter_build.py check readable and free of GitHub Actions
# quoting issues. The workflow's `command:` is reduced to
# `bash tools/build_presenter_firmware.sh` which keeps the outer
# `bash -c '...'` wrapper trivial.
#
# This script must be invoked from the repository root inside the
# espressif/esp-idf-ci-action Docker image so that idf.py, python3 and
# the host project are all available.

set -eu

BUILD_DIR=${BUILD_DIR:-/tmp/presenter-build}
MERGED_BIN="$BUILD_DIR/FoloToy-AI-Passport-full.bin"
SDKCONFIG_DEFAULTS_VALUE="sdkconfig.defaults;sdkconfig.presenter.defaults"
export SDKCONFIG_DEFAULTS="$SDKCONFIG_DEFAULTS_VALUE"

echo "[build_presenter_firmware] BUILD_DIR=$BUILD_DIR"
echo "[build_presenter_firmware] SDKCONFIG_DEFAULTS=$SDKCONFIG_DEFAULTS"

# 1. set-target populates the build directory and the sdkconfig file from
#    the layered SDKCONFIG_DEFAULTS. CMake's -D mirrors the env var into
#    the cache as a defensive fallback in case the env var is ever
#    stripped by the action's docker run layer.
idf.py -B "$BUILD_DIR" \
    -D "SDKCONFIG_DEFAULTS=$SDKCONFIG_DEFAULTS_VALUE" \
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