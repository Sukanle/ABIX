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


def run(
    cmd: list[str], cwd: Path | None = None, env: dict[str, str] | None = None
) -> None:
    print(f"  \033[90m{' '.join(cmd)}\033[0m")
    # Merge into the ambient environment instead of replacing it, so `env` only
    # adds/overrides variables (dropping PATH would break compiler lookups).
    child_env = None if env is None else {**os.environ, **env}
    result = subprocess.run(cmd, cwd=cwd, env=child_env)
    if result.returncode != 0:
        raise SystemExit(
            f"Command failed (exit code {result.returncode}): {' '.join(cmd)}"
        )


def parse_key_values(items: list[str], flag: str) -> dict[str, str]:
    """Parse repeatable `KEY=VALUE` flags into a dict (the last value wins)."""
    parsed: dict[str, str] = {}
    for item in items:
        key, separator, value = item.partition("=")
        if not separator or not key:
            raise SystemExit(f"{flag} expects KEY=VALUE, got: {item!r}")
        parsed[key] = value
    return parsed


def get_executable_name(base: str) -> str:
    """Add .exe on Windows, otherwise return as-is."""
    return f"{base}.exe" if sys.platform == "win32" else base


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Configure, build and run cross-DLL mics test suite"
    )
    parser.add_argument(
        "--clean", action="store_true", help="Clean build directory before building"
    )
    parser.add_argument(
        "--run-test",
        action="store_true",
        help="Run tests only, skip build and benchmarks",
    )
    parser.add_argument(
        "--run-bench",
        action="store_true",
        help="Run benchmarks only, skip build and tests",
    )
    parser.add_argument(
        "--run-only",
        action="store_true",
        help="Run both tests and benchmarks, skip build",
    )
    parser.add_argument(
        "--build-type", default="Release", help="CMake build type (Release/Debug)"
    )
    parser.add_argument(
        "--build-test",
        action="store_true",
        help="Build tests only, skip run and benchmarks",
    )
    parser.add_argument(
        "--build-bench",
        action="store_true",
        help="Build benchmarks only, skip run and tests",
    )
    parser.add_argument(
        "--build-only",
        action="store_true",
        help="Build both tests and benchmarks, skip run",
    )
    parser.add_argument(
        "--generator", default="", help="CMake generator (auto-detect by default)"
    )
    parser.add_argument(
        "--with-dev", action="store_true", help="Build with development features"
    )
    parser.add_argument(
        "--cmake-env",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="Environment variable for the CMake configure/build (repeatable)",
    )
    parser.add_argument(
        "--cmake-var",
        action="append",
        default=[],
        metavar="KEY=VALUE",
        help="CMake cache variable, forwarded as -DKEY=VALUE (repeatable)",
    )
    parser.add_argument(
        "--bench-repetitions",
        type=int,
        default=5,
        help="Google Benchmark repetitions for the per-family runner",
    )
    parser.add_argument(
        "--bench-out",
        default="",
        help="directory for the per-family benchmark JSON (default: <build>/bench)",
    )
    parser.add_argument(
        "--bench-min-time",
        default="",
        help="optional bound for each benchmark run, e.g. 0.2s",
    )
    parser.add_argument(
        "--bench-raw",
        action="store_true",
        help="Run bench_all in a single process instead of one process per family",
    )
    if sys.platform == "win32":
        parser.add_argument(
            "--with-msvc",
            action="store_true",
            help="Also build MSVC cross-compiler variants (for Test 13)",
        )
        parser.add_argument(
            "--vs-path",
            default="",
            help="Visual Studio installation path (use with --with-msvc)",
        )
    args = parser.parse_args()

    cmake_env = parse_key_values(args.cmake_env, "--cmake-env")
    cmake_vars = parse_key_values(args.cmake_var, "--cmake-var")

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

    if (cmake_env or cmake_vars) and not should_build:
        parser.error("--cmake-env/--cmake-var require a build step (cannot use --run-*)")

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

        configure = [
            "cmake",
            "-S",
            str(PROJECT_ROOT),
            "-B",
            str(build_dir),
            "-G",
            generator,
            f"-DCMAKE_BUILD_TYPE={args.build_type}",
            f"-DSKL_ABIX_DEVELOPMENT={'ON' if args.with_dev else 'OFF'}",
        ]
        configure.extend(f"-D{key}={value}" for key, value in cmake_vars.items())
        run(configure, env=cmake_env)
        run(["cmake", "--build", str(build_dir), "--parallel"], env=cmake_env)

        print("== Build complete ==")

    variants_script = CURRENT_DIR / "build_variants.py"
    if not variants_script.exists():
        raise SystemExit(f"Variant build script not found: {variants_script}")
    print(f"\n== Running variant build: {variants_script} ==")
    variants_cmd = [
        sys.executable,
        str(variants_script),
        "--build-type",
        args.build_type,
    ]
    if sys.platform == "win32" and args.with_msvc:
        variants_cmd.append("--with-msvc")
        if args.vs_path:
            variants_cmd.extend(["--vs-path", args.vs_path])
    result = subprocess.run(variants_cmd)
    if result.returncode != 0:
        raise SystemExit(f"Variant build failed, exit code {result.returncode}")

    if should_test:
        print("\n== Running tests ==")
        test_exe = build_dir / "bin" / get_executable_name("test_all")
        if not test_exe.exists():
            raise SystemExit(f"Test executable not found: {test_exe}")
        run([str(test_exe)], cwd=build_dir / "bin")

    if should_bench:
        bench_exe = build_dir / "bin" / get_executable_name("bench_all")
        if not bench_exe.exists():
            raise SystemExit(f"Benchmark executable not found: {bench_exe}")
        if args.bench_raw:
            # One process, everything at once: what build.py did before the bench
            # runner was extracted. Fast, but the EBR/RCU families share the
            # process-wide rcu::Domain, so a family measured after the others have
            # run reads several times slower (see tools/run_bench.py).
            print("\n== Running benchmarks (single process) ==")
            run([str(bench_exe)], cwd=build_dir / "bin")
        else:
            # One process per family, via the extracted runner: comparable numbers,
            # and it also covers the standalone Google Benchmark executables and the
            # families that are not EBR/RCU.
            print("\n== Running benchmarks (one process per family) ==")
            runner = CURRENT_DIR / "run_bench.py"
            if not runner.exists():
                raise SystemExit(f"Benchmark runner not found: {runner}")
            bench_out = Path(args.bench_out) if args.bench_out else build_dir / "bench"
            bench_cmd = [
                sys.executable,
                str(runner),
                "--bin",
                str(build_dir / "bin"),
                "--out",
                str(bench_out),
                "--repetitions",
                str(args.bench_repetitions),
            ]
            if args.bench_min_time:
                bench_cmd.extend(["--min-time", args.bench_min_time])
            run(bench_cmd)


if __name__ == "__main__":
    main()
