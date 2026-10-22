#!/usr/bin/env python3
"""Build script for the ABIX project.

Responsibilities:
  1. CMake configure + build
  2. Generate hotcache_dll.cpp (via gen_hotcache_dll.py)
  3. Build cross-compiler DLL variants (via build_variants.py)

This script does NOT run tests or benchmarks. Use:
  - python tools/run_test.py    for tests
  - python tools/run_bench.py   for benchmarks
  - python tools/run.py         for the full pipeline (build + test + bench)

Usage:
    python tools/run_build.py                          # default build
    python tools/run_build.py --clean                  # clean + build
    python tools/run_build.py --build-type Debug       # Debug build
    python tools/run_build.py --with-dev               # development features
    python tools/run_build.py --with-msvc              # include MSVC variants (Windows)
    python tools/run_build.py --cmake-var KEY=VALUE   # pass CMake variables
"""

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
    """Run a command and raise SystemExit on failure."""
    print(f"  \033[90m{' '.join(cmd)}\033[0m")
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


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Configure, build and generate DLL variants for the ABIX project",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--clean", action="store_true", help="Clean build directory before building"
    )
    parser.add_argument(
        "--build-type", default="Release", help="CMake build type (Release/Debug)"
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
        "--skip-variants",
        action="store_true",
        help="Skip cross-compiler DLL variant build",
    )
    parser.add_argument(
        "--skip-hotcache",
        action="store_true",
        help="Skip hotcache_dll.cpp generation",
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

    # --- Step 0: Clean (optional) ---
    if args.clean and build_dir.exists():
        print("== Cleaning build directory ==")
        shutil.rmtree(build_dir)

    # --- Step 1: CMake configure ---
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

    # --- Step 2: CMake build ---
    run(["cmake", "--build", str(build_dir), "--parallel"], env=cmake_env)
    print("== Build complete ==")

    # --- Step 3: Generate hotcache_dll.cpp ---
    if not args.skip_hotcache:
        gen_hotcache = CURRENT_DIR / "gen_hotcache_dll.py"
        if not gen_hotcache.exists():
            raise SystemExit(f"gen_hotcache_dll.py not found: {gen_hotcache}")
        print(f"\n== Generating hotcache_dll.cpp: {gen_hotcache} ==")
        run([sys.executable, str(gen_hotcache)])

    # --- Step 4: Build cross-compiler DLL variants ---
    if not args.skip_variants:
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
        run(variants_cmd)

    print("\n== All build steps completed successfully ==")
    return 0


if __name__ == "__main__":
    sys.exit(main())
