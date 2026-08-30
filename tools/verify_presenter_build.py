#!/usr/bin/env python3
"""Verify PC Controller presenter firmware build artifacts.

This wraps the legacy grep-based profile checks and tools/verify_firmware.py
into a single command that the GitHub Actions workflow can invoke directly,
so the step's exit code reflects the first failing check without depending
on bash ``&&`` chaining or shell quoting tricks. Replaces the prior
single-line ``grep && grep && python3`` invocation that exited with code 2
on the first non-zero return.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


REQUIRED_SDKCONFIG_LINES = (
    ("CONFIG_PC_CONTROLLER_APP=y", r"^CONFIG_PC_CONTROLLER_APP=y\s*$"),
    ('CONFIG_IDF_TARGET="esp32c3"', r'^CONFIG_IDF_TARGET="esp32c3"\s*$'),
)


def check_sdkconfig(sdkconfig_path: Path) -> list[str]:
    """Return a list of human-readable errors; empty list means PASS."""
    if not sdkconfig_path.is_file():
        return [f"sdkconfig not found at {sdkconfig_path}"]

    text = sdkconfig_path.read_text(encoding="utf-8", errors="replace")
    errors: list[str] = []
    for label, pattern in REQUIRED_SDKCONFIG_LINES:
        if not re.search(pattern, text, re.MULTILINE):
            errors.append(f"sdkconfig is missing required line: {label}")
    return errors


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    build_dir = Path(
        sys.argv[1] if len(sys.argv) > 1 else "/tmp/presenter-build"
    ).resolve()
    sdkconfig_path = build_dir / "sdkconfig"

    sdkconfig_errors = check_sdkconfig(sdkconfig_path)
    if sdkconfig_errors:
        print("Presenter profile sdkconfig checks: FAIL", file=sys.stderr)
        for err in sdkconfig_errors:
            print(f"  - {err}", file=sys.stderr)
        # 2 keeps the original step's failure code so downstream tooling that
        # distinguished exit 1 (script bug) from exit 2 (artifact mismatch) keeps
        # working.
        return 2

    print(
        "Presenter profile sdkconfig checks: PASS "
        f"(CONFIG_PC_CONTROLLER_APP=y, CONFIG_IDF_TARGET=\"esp32c3\" present in {sdkconfig_path})"
    )

    verify_script = repo_root / "tools" / "verify_firmware.py"
    if not verify_script.is_file():
        print(f"ERROR: {verify_script} is missing", file=sys.stderr)
        return 1

    result = subprocess.run(
        [sys.executable, str(verify_script), str(build_dir)],
        cwd=str(repo_root),
    )
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
