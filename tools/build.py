#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent
CURRENT_DIR = Path(__file__).resolve().parent

def run(cmd: list[str], cwd: Path | None = None) -> None:
    print(f"  \033[90m{' '.join(cmd)}\033[0m")
    result = subprocess.run(cmd, cwd=cwd)
    if result.returncode != 0:
        raise SystemExit(f"Command failed (exit code {result.returncode}): {' '.join(cmd)}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Configure, build and run cross-DLL reflection test suite")
    parser.add_argument("--clean", action="store_true", help="Clean build directory before building")
    parser.add_argument("--run-test", action="store_true", help="Run tests only, skip build and benchmarks")
    parser.add_argument("--run-bench", action="store_true", help="Run benchmarks only, skip build and tests")
    parser.add_argument("--run-only", action="store_true", help="Run both tests and benchmarks, skip build")
    parser.add_argument("--generator", default="", help="CMake generator (auto-detect by default)")
    parser.add_argument("--with-msvc", action="store_true", help="Also build MSVC cross-compiler variants (for Test 13)")
    parser.add_argument("--vs-path", default="", help="Visual Studio installation path (use with --with-msvc)")
    args = parser.parse_args()

    build_dir = PROJECT_ROOT / "build"

    if args.clean and build_dir.exists():
        print("== Cleaning build directory ==")
        shutil.rmtree(build_dir)

    if not args.run_only:
        generator = args.generator
        if not generator:
            if shutil.which("ninja"):
                generator = "Ninja"
            else:
                generator = "MinGW Makefiles"
        print(f"== Using generator: {generator} ==")

        run([
            "cmake", "-S", str(PROJECT_ROOT), "-B", str(build_dir),
            "-G", generator, "-DCMAKE_BUILD_TYPE=Release",
        ])
        run(["cmake", "--build", str(build_dir), "-j"])

        print("== Build complete ==")

    variants_script = CURRENT_DIR / "build_variants.py"
    print(f"\n== Running variant build: {variants_script} ==")
    result = subprocess.run([sys.executable, str(variants_script)])
    if result.returncode != 0:
        raise SystemExit(f"Variant build failed, exit code {result.returncode}")

    if args.with_msvc:
        msvc_script = CURRENT_DIR / "build_msvc_variants.ps1"
        print(f"\n== Running MSVC variant build: {msvc_script} ==")
        msvc_cmd = ["powershell", msvc_script]
        if args.vs_path:
            msvc_cmd.extend(["--vs-path", args.vs_path])
        result = subprocess.run(msvc_cmd)
        if result.returncode != 0:
            print("  [Warning] MSVC variant build failed (can be ignored if VS is not installed), cross-CRT tests will be skipped")

    run_test = args.run_test
    run_bench = args.run_bench

    if args.run_only:
        run_test = True
        run_bench = True

    if run_test:
        print("\n== Running tests ==")
        test_exe = build_dir / "bin" / "test_all.exe"
        if not test_exe.exists():
            raise SystemExit(f"Test executable not found: {test_exe}")
        run([str(test_exe)], cwd=build_dir / "bin")

    if run_bench:
        print("\n== Running benchmarks ==")
        bench_exe = build_dir / "bin" / "bench_all.exe"
        if not bench_exe.exists():
            raise SystemExit(f"Benchmark executable not found: {bench_exe}")
        run([str(bench_exe)], cwd=build_dir / "bin")

if __name__ == "__main__":
    main()