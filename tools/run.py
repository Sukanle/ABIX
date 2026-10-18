#!/usr/bin/env python3
"""Unified entry point for the ABIX project.

Orchestrates the full pipeline: build → test → bench.

Usage:
    python tools/run.py                        # build + test + bench
    python tools/run.py --build-only           # build only
    python tools/run.py --test-only            # test only (skip build)
    python tools/run.py --bench-only           # bench only (skip build)
    python tools/run.py --build-test           # build + test (skip bench)
    python tools/run.py --build-bench          # build + bench (skip test)
    python tools/run.py --clean                # clean + full pipeline
    python tools/run.py --build-type Debug     # Debug build
    python tools/run.py --with-dev             # development features
    python tools/run.py --bench-raw            # single-process bench
    python tools/run.py --bench-pin off        # disable CPU pinning for bench

All build/test/bench options are forwarded to the respective scripts:
  - run_build.py:  --clean, --build-type, --generator, --with-dev, --cmake-var, ...
  - run_test.py:   --catch2, --elf-pe, --bootstrap, --keep, ...
  - run_bench.py:  --bench-raw, --bench-pin, --bench-repetitions, ...
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent


def run_script(script_name: str, args: list[str]) -> int:
    """Run a Python script from the tools directory."""
    script = TOOLS_DIR / script_name
    if not script.exists():
        print(f"\033[31mError: script not found: {script}\033[0m", file=sys.stderr)
        return 1
    cmd = [sys.executable, str(script)] + args
    print(f"\n\033[1m>>> {' '.join(cmd)}\033[0m")
    result = subprocess.run(cmd)
    return result.returncode


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Unified entry point for the ABIX project (build + test + bench)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
Pipeline stages:
  build     Configure, build, generate hotcache_dll, build DLL variants
  test      Catch2 tests + ELF/PE metadata verification + AMC bootstrap
  bench     Google Benchmark (one process per family)

Examples:
  python tools/run.py                        # full pipeline
  python tools/run.py --build-only           # build only
  python tools/run.py --test-only            # test only
  python tools/run.py --bench-only           # bench only
  python tools/run.py --build-test           # build + test
  python tools/run.py --clean --build-type Debug
  python tools/run.py --test-only --catch2   # Catch2 tests only
  python tools/run.py --bench-only --bench-raw
""",
    )

    # Pipeline control
    pipeline = parser.add_mutually_exclusive_group()
    pipeline.add_argument(
        "--build-only", action="store_true", help="Build only, skip test and bench"
    )
    pipeline.add_argument(
        "--test-only", action="store_true", help="Test only, skip build and bench"
    )
    pipeline.add_argument(
        "--bench-only", action="store_true", help="Bench only, skip build and test"
    )
    pipeline.add_argument(
        "--build-test", action="store_true", help="Build + test, skip bench"
    )
    pipeline.add_argument(
        "--build-bench", action="store_true", help="Build + bench, skip test"
    )

    # Build options (forwarded to run_build.py)
    build_group = parser.add_argument_group("build options (forwarded to run_build.py)")
    build_group.add_argument(
        "--clean", action="store_true", help="Clean build directory before building"
    )
    build_group.add_argument(
        "--build-type", default="Release", help="CMake build type (Release/Debug)"
    )
    build_group.add_argument(
        "--generator", default="", help="CMake generator (auto-detect by default)"
    )
    build_group.add_argument(
        "--with-dev", action="store_true", help="Build with development features"
    )
    build_group.add_argument(
        "--cmake-env",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="Environment variable for CMake (repeatable)",
    )
    build_group.add_argument(
        "--cmake-var",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="CMake cache variable (repeatable)",
    )
    build_group.add_argument(
        "--skip-variants", action="store_true", help="Skip DLL variant build"
    )
    build_group.add_argument(
        "--skip-hotcache", action="store_true", help="Skip hotcache_dll.cpp generation"
    )
    if sys.platform == "win32":
        build_group.add_argument(
            "--with-msvc", action="store_true", help="Build MSVC variants (Windows)"
        )
        build_group.add_argument(
            "--vs-path", default="", help="Visual Studio path (Windows)"
        )

    # Test options (forwarded to run_test.py)
    test_group = parser.add_argument_group("test options (forwarded to run_test.py)")
    test_group.add_argument(
        "--catch2", action="store_true", help="Run Catch2 tests only"
    )
    test_group.add_argument(
        "--elf-pe", action="store_true", help="Run ELF/PE metadata tests only"
    )
    test_group.add_argument(
        "--bootstrap", action="store_true", help="Run AMC bootstrap test only"
    )
    test_group.add_argument(
        "--interop", action="store_true", help="Run cross-language interop tests only"
    )
    test_group.add_argument(
        "--keep", action="store_true", help="Keep temporary test files"
    )
    test_group.add_argument(
        "--cmdly-root",
        default="",
        metavar="PATH",
        help="Path to the cmdly checkout for the interop cmdly comparison (sets CMDLY_ROOT)",
    )

    # Bench options (forwarded to run_bench.py)
    bench_group = parser.add_argument_group("bench options (forwarded to run_bench.py)")
    bench_group.add_argument(
        "--bench-raw",
        action="store_true",
        help="Run bench_all in a single process instead of one per family",
    )
    bench_group.add_argument(
        "--bench-repetitions",
        type=int,
        default=5,
        help="Google Benchmark repetitions (default: 5)",
    )
    bench_group.add_argument(
        "--bench-out",
        default="",
        help="Directory for benchmark JSON output",
    )
    bench_group.add_argument(
        "--bench-min-time",
        default="",
        help="Minimum time per benchmark (e.g. 0.2s)",
    )
    bench_group.add_argument(
        "--bench-pin",
        default=None,
        help="CPU pinning: auto, off, or a list like 0-7",
    )

    args = parser.parse_args()

    # Determine which stages to run
    if args.build_only:
        should_build, should_test, should_bench = True, False, False
    elif args.test_only:
        should_build, should_test, should_bench = False, True, False
    elif args.bench_only:
        should_build, should_test, should_bench = False, False, True
    elif args.build_test:
        should_build, should_test, should_bench = True, True, False
    elif args.build_bench:
        should_build, should_test, should_bench = True, False, True
    else:
        should_build = should_test = should_bench = True

    exit_code = 0

    # --- Build stage ---
    if should_build:
        print("\n" + "=" * 70)
        print("  BUILD STAGE")
        print("=" * 70)
        build_args = [
            "--build-type", args.build_type,
        ]
        if args.clean:
            build_args.append("--clean")
        if args.generator:
            build_args.extend(["--generator", args.generator])
        if args.with_dev:
            build_args.append("--with-dev")
        if args.skip_variants:
            build_args.append("--skip-variants")
        if args.skip_hotcache:
            build_args.append("--skip-hotcache")
        for item in args.cmake_env:
            build_args.extend(["--cmake-env", item])
        for item in args.cmake_var:
            build_args.extend(["--cmake-var", item])
        if sys.platform == "win32":
            if args.with_msvc:
                build_args.append("--with-msvc")
            if args.vs_path:
                build_args.extend(["--vs-path", args.vs_path])

        rc = run_script("run_build.py", build_args)
        if rc != 0:
            print(f"\033[31mBuild failed with exit code {rc}\033[0m", file=sys.stderr)
            return rc

    # --- Test stage ---
    if should_test:
        print("\n" + "=" * 70)
        print("  TEST STAGE")
        print("=" * 70)
        test_args = []
        if args.catch2:
            test_args.append("--catch2")
        if args.elf_pe:
            test_args.append("--elf-pe")
        if args.bootstrap:
            test_args.append("--bootstrap")
        if args.interop:
            test_args.append("--interop")
        if args.keep:
            test_args.append("--keep")
        if args.cmdly_root:
            test_args.extend(["--cmdly-root", args.cmdly_root])
        if args.build_type != "Release":
            test_args.extend(["--build-dir", f"build/{args.build_type}"])

        rc = run_script("run_test.py", test_args)
        if rc != 0:
            print(f"\033[31mTests failed with exit code {rc}\033[0m", file=sys.stderr)
            exit_code = rc

    # --- Bench stage ---
    if should_bench:
        print("\n" + "=" * 70)
        print("  BENCH STAGE")
        print("=" * 70)
        build_dir = TOOLS_DIR.parent / "build" / args.build_type
        bin_dir = build_dir / "bin"

        if args.bench_raw:
            # Single-process mode: run bench_all directly
            bench_all = bin_dir / ("bench_all.exe" if sys.platform == "win32" else "bench_all")
            if not bench_all.exists():
                print(f"\033[31mError: bench_all not found at {bench_all}\033[0m", file=sys.stderr)
                return 1
            print(f"\n\033[1m>>> Running benchmarks (single process)\033[0m")
            result = subprocess.run([str(bench_all)], cwd=str(bin_dir))
            if result.returncode != 0:
                print(f"\033[31mBench failed with exit code {result.returncode}\033[0m", file=sys.stderr)
                exit_code = result.returncode
        else:
            # Per-family mode: use run_bench.py
            bench_args = []
            if args.bench_repetitions != 5:
                bench_args.extend(["--repetitions", str(args.bench_repetitions)])
            if args.bench_out:
                bench_args.extend(["--out", args.bench_out])
            else:
                bench_args.extend(["--out", str(build_dir / "bench")])
            if args.bench_min_time:
                bench_args.extend(["--min-time", args.bench_min_time])
            if args.bench_pin is not None:
                bench_args.extend(["--pin", args.bench_pin])
            bench_args.extend(["--bin", str(bin_dir)])

            rc = run_script("run_bench.py", bench_args)
            if rc != 0:
                print(f"\033[31mBench failed with exit code {rc}\033[0m", file=sys.stderr)
                exit_code = rc

    # --- Summary ---
    print("\n" + "=" * 70)
    if exit_code == 0:
        print("  \033[32mAll stages completed successfully.\033[0m")
    else:
        print(f"  \033[31mPipeline completed with errors (exit code {exit_code}).\033[0m")
    print("=" * 70)

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
