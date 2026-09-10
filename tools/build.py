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

def get_executable_name(base: str) -> str:
    """Add .exe on Windows, otherwise return as-is."""
    return f"{base}.exe" if sys.platform == "win32" else base

def main() -> None:
    parser = argparse.ArgumentParser(description="Configure, build and run cross-DLL reflection test suite")
    parser.add_argument("--clean", action="store_true", help="Clean build directory before building")
    parser.add_argument("--run-test", action="store_true", help="Run tests only, skip build and benchmarks")
    parser.add_argument("--run-bench", action="store_true", help="Run benchmarks only, skip build and tests")
    parser.add_argument("--run-only", action="store_true", help="Run both tests and benchmarks, skip build")
    parser.add_argument("--build-type", default="Release", help="CMake build type (Release/Debug)")
    parser.add_argument("--build-test", action="store_true", help="Build tests only, skip run and benchmarks")
    parser.add_argument("--build-bench", action="store_true", help="Build benchmarks only, skip run and tests")
    parser.add_argument("--build-only", action="store_true", help="Build both tests and benchmarks, skip run")
    parser.add_argument("--generator", default="", help="CMake generator (auto-detect by default)")
    parser.add_argument("--with-dev", action="store_true", help="Build with development features")
    if sys.platform == "win32":
        parser.add_argument("--with-msvc", action="store_true", help="Also build MSVC cross-compiler variants (for Test 13)")
        parser.add_argument("--vs-path", default="", help="Visual Studio installation path (use with --with-msvc)")
    args = parser.parse_args()

    build_dir = PROJECT_ROOT / "build" / args.build_type

    if args.clean and build_dir.exists():
        print("== Cleaning build directory ==")
        shutil.rmtree(build_dir)
    
    build_flags = any([args.build_test, args.build_bench, args.build_only])
    run_flags = any([args.run_test, args.run_bench, args.run_only])

    if build_flags and run_flags:
        parser.error("Cannot mix --build-* and --run-* flags")

    if run_flags:
        should_build = False
        should_test = args.run_test or args.run_only
        should_bench = args.run_bench or args.run_only
    elif build_flags:
        should_build = True
        should_test = args.build_test
        should_bench = args.build_bench
    else:
        should_build = should_test = should_bench = True

    if should_build:
        generator = args.generator
        if not generator:
            if shutil.which("ninja"):
                generator = "Ninja"
            elif sys.platform == "win32":
                generator = "MinGW Makefiles"
            else:
                generator = "Unix Makefiles"

        print(f"== Using generator: {generator} ==")

        run([
            "cmake", "-S", str(PROJECT_ROOT), "-B", str(build_dir),
            "-G", generator, f"-DCMAKE_BUILD_TYPE={args.build_type}",
            f"-DSKL_ABIX_DEVELOPMENT={'ON' if args.with_dev else 'OFF'}",
        ])
        run(["cmake", "--build", str(build_dir), "--parallel"])

        print("== Build complete ==")

    variants_script = CURRENT_DIR / "build_variants.py"
    if not variants_script.exists():
        raise SystemExit(f"Variant build script not found: {variants_script}")
    print(f"\n== Running variant build: {variants_script} ==")
    result = subprocess.run([sys.executable, str(variants_script), "--build-type", args.build_type])
    if result.returncode != 0:
        raise SystemExit(f"Variant build failed, exit code {result.returncode}")

    if sys.platform == "win32" and args.with_msvc:
        msvc_script = CURRENT_DIR / "build_msvc_variants.ps1"
        if not msvc_script.exists():
            print(f"  [Warning] MSVC variant script not found: {msvc_script}")
        else:
            print(f"\n== Running MSVC variant build: {msvc_script} ==")
            msvc_cmd = ["powershell", "-File", str(msvc_script)]
            if args.vs_path:
                msvc_cmd.extend(["-VsPath", args.vs_path])
            result = subprocess.run(msvc_cmd)
            if result.returncode != 0:
                print("  [Warning] MSVC variant build failed (can be ignored if VS is not installed), cross-CRT tests will be skipped")

    if should_test:
        print("\n== Running tests ==")
        test_exe = build_dir / "bin" / get_executable_name("test_all")
        if not test_exe.exists():
            raise SystemExit(f"Test executable not found: {test_exe}")
        run([str(test_exe)], cwd = build_dir / "bin")

    if should_bench:
        print("\n== Running benchmarks ==")
        bench_exe = build_dir / "bin" / get_executable_name("bench_all")
        if not bench_exe.exists():
            raise SystemExit(f"Benchmark executable not found: {bench_exe}")
        run([str(bench_exe)], cwd = build_dir / "bin")

if __name__ == "__main__":
    main()