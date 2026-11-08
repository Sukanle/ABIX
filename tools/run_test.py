#!/usr/bin/env python3
"""Run the ABIX CTest suite.

`ctest` covers every registered test: the unity `test_all` cases, the standalone
suites (lease, STD contract, owner, interface, metadata, ...) and the CMake
package test. AMC suites live in the AMC repository.

Usage:
    python tools/run_test.py
    python tools/run_test.py --build-dir build/Debug
    python tools/run_test.py --catch2        # quick test_all smoke only
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = TOOLS_DIR.parent


def exe_name(base: str) -> str:
    return f"{base}.exe" if sys.platform == "win32" else base


def run_ctest(build_dir: Path) -> int:
    """Run the full CTest suite registered for `build_dir`."""
    ctest = shutil.which("ctest")
    if ctest is None:
        print(
            "\033[31mError: ctest not found; install CMake and put it on PATH\033[0m",
            file=sys.stderr,
        )
        return 1

    test_file = build_dir / "CTestTestfile.cmake"
    if not test_file.exists():
        print(f"\033[31mError: CTest file not found: {test_file}\033[0m", file=sys.stderr)
        print("Build first: python tools/run_build.py")
        return 1

    print("\n" + "=" * 60, flush=True)
    print("ABIX CTest Suite", flush=True)
    print("=" * 60, flush=True)
    result = subprocess.run([ctest, "--test-dir", str(build_dir), "--output-on-failure"])
    return result.returncode


def run_catch2_tests(build_dir: Path) -> int:
    """Run only the unity `test_all` executable (quick smoke)."""
    print("\n" + "=" * 60, flush=True)
    print("Catch2 test_all (quick smoke)", flush=True)
    print("=" * 60, flush=True)

    test_exe = build_dir / "bin" / exe_name("test_all")
    if not test_exe.exists():
        print(f"\033[31mError: test executable not found: {test_exe}\033[0m", file=sys.stderr)
        print("Build first: python tools/run_build.py")
        return 1

    result = subprocess.run([str(test_exe)], cwd=build_dir / "bin")
    return result.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the ABIX CTest suite")
    parser.add_argument(
        "--build-dir",
        default="build/Release",
        help="Build directory (default: build/Release)",
    )
    parser.add_argument(
        "--catch2",
        action="store_true",
        help="Run only the test_all unity executable (quick smoke, not the full CTest suite)",
    )
    args = parser.parse_args()

    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = PROJECT_ROOT / build_dir

    if args.catch2:
        return run_catch2_tests(build_dir)
    return run_ctest(build_dir)


if __name__ == "__main__":
    sys.exit(main())
