#!/usr/bin/env python3
"""Run the ABIX Catch2 test suite.

AMC toolchain tests (ELF/PE metadata, metadata bootstrap, MCP compatibility and
cross-language interop) live in the AMC repository:
<https://github.com/Sukanle/AMC>. In particular, AMC registers its suites in
CTest, so run `ctest --test-dir <amc-build>` there.

Usage:
    python tools/run_test.py
    python tools/run_test.py --build-dir build/Debug
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = TOOLS_DIR.parent


def exe_name(base: str) -> str:
    return f"{base}.exe" if sys.platform == "win32" else base


def run_catch2_tests(build_dir: Path) -> int:
    """Run the Catch2 test_all executable."""
    print("\n" + "=" * 60)
    print("Catch2 C++ Test Suite")
    print("=" * 60)

    test_exe = build_dir / "bin" / exe_name("test_all")
    if not test_exe.exists():
        print(f"\033[31mError: test executable not found: {test_exe}\033[0m")
        print("Build first: python tools/run_build.py")
        return 1

    result = subprocess.run([str(test_exe)], cwd=build_dir / "bin")
    return result.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the ABIX Catch2 test suite")
    parser.add_argument(
        "--build-dir",
        default="build/Release",
        help="Build directory (default: build/Release)",
    )
    parser.add_argument(
        "--catch2",
        action="store_true",
        help="Accepted for compatibility; the Catch2 suite is the only ABIX suite",
    )
    args = parser.parse_args()

    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = PROJECT_ROOT / build_dir

    return run_catch2_tests(build_dir)


if __name__ == "__main__":
    sys.exit(main())
