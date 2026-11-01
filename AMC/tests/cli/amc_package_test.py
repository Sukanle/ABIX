#!/usr/bin/env python3
"""Install AMC into a temporary prefix and consume it with find_package(AMC)."""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile


def run(command, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, **kwargs)
    if result.returncode != 0:
        print("command failed:", " ".join(command), file=sys.stderr)
        if result.stdout:
            print(result.stdout, file=sys.stderr)
        if result.stderr:
            print(result.stderr, file=sys.stderr)
        raise SystemExit(result.returncode)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", help="Configured AMC build directory")
    parser.add_argument("source_root", help="AMC source root")
    parser.add_argument("--abix-dir", required=True, help="Installed ABIX CMake package directory")
    parser.add_argument("--fmt-dir", default=os.environ.get("fmt_DIR", ""), help="fmt CMake package directory")
    parser.add_argument("--cxx", help="C++ compiler for the package consumer")
    parser.add_argument("--keep-artifacts", action="store_true")
    args = parser.parse_args()

    cmake = shutil.which("cmake")
    if not cmake:
        print("  [SKIP] cmake not found")
        return 0

    work = tempfile.mkdtemp(prefix="amc-package.")
    prefix = os.path.join(work, "prefix")
    consumer = os.path.join(work, "consumer")
    os.makedirs(consumer)

    try:
        run([cmake, "--install", os.path.abspath(args.build_dir), "--prefix", prefix])

        fixture_dir = os.path.join(os.path.abspath(args.source_root), "tests", "fixtures")
        cli_dir = os.path.join(os.path.abspath(args.source_root), "tests", "cli", "cmake_fixture")
        shutil.copy(os.path.join(fixture_dir, "amc_test.abic.toml"), consumer)
        shutil.copy(os.path.join(fixture_dir, "amc_test_types.hpp"), consumer)
        shutil.copy(os.path.join(cli_dir, "consumer.cpp"), consumer)

        with open(os.path.join(consumer, "CMakeLists.txt"), "w", encoding="utf-8") as handle:
            handle.write(
                "cmake_minimum_required(VERSION 3.20)\n"
                "project(amc_package_consumer LANGUAGES CXX)\n"
                "set(CMAKE_CXX_STANDARD 17)\n"
                "find_package(AMC 0.1 CONFIG REQUIRED)\n"
                "add_library(amc_cmake_consumer STATIC consumer.cpp)\n"
                "amc_add_abi(\n"
                "  TARGET amc_cmake_consumer\n"
                "  CONFIG \"${CMAKE_CURRENT_SOURCE_DIR}/amc_test.abic.toml\"\n"
                "  ABIX_OUTPUT \"${CMAKE_CURRENT_BINARY_DIR}/amc-output/build/amc_test.abix\"\n"
                "  CPP_OUTPUT \"${CMAKE_CURRENT_BINARY_DIR}/generated/amc_metadata.hpp\"\n"
                "  DEPENDS \"${CMAKE_CURRENT_SOURCE_DIR}/amc_test_types.hpp\")\n"
            )

        configure = [
            cmake,
            "-S", consumer,
            "-B", os.path.join(consumer, "build"),
            "-G", "Ninja",
            f"-DCMAKE_PREFIX_PATH={prefix}",
            f"-DABIX_DIR={os.path.abspath(args.abix_dir)}",
        ]
        if args.fmt_dir:
            configure.append(f"-Dfmt_DIR={args.fmt_dir}")
        if args.cxx:
            configure.append(f"-DCMAKE_CXX_COMPILER={args.cxx}")
        run(configure)
        run([cmake, "--build", os.path.join(consumer, "build")])

        abix_output = os.path.join(consumer, "build", "amc-output", "build", "amc_test.abix")
        hpp_output = os.path.join(consumer, "build", "generated", "amc_metadata.hpp")
        if not os.path.isfile(abix_output) or not os.path.isfile(hpp_output):
            print("  [FAIL] AMC package consumer did not produce the expected artifacts")
            return 1

        installed_amc = os.path.join(prefix, "bin", "amc.exe" if sys.platform == "win32" else "amc")
        result = run([installed_amc, "--list-languages"])
        if "cpp" not in result.stdout:
            print("  [FAIL] installed amc did not list the C++ provider")
            return 1

        print("  [PASS] find_package(AMC) consumer configured, built and ran")
        print("  [PASS] installed amc --list-languages works")
        return 0
    finally:
        if args.keep_artifacts or os.environ.get("KEEP_ARTIFACTS", "") == "1":
            print(f"  [KEEP] retained artifacts: {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
