#!/usr/bin/env python3
"""CMake integration test for AMC.

Usage:
    amc_cmake_integration_test.py <amc_bin_dir> <source_root>

Behaviour:
    - Configures and builds a CMake fixture project using Ninja.
    - Verifies that the expected .abix artifact and generated header exist.
"""

import argparse
import os
import subprocess
import sys
import tempfile
import shutil


def main() -> None:
    parser = argparse.ArgumentParser(
        description="AMC CMake integration test"
    )
    parser.add_argument("amc_bin_dir", help="Directory containing amc executable")
    parser.add_argument("source_root", help="Root of the ABIX source tree")
    args = parser.parse_args()

    amc_bin = os.path.abspath(args.amc_bin_dir)
    amc_root = os.path.abspath(args.source_root)

    build_dir = tempfile.mkdtemp(prefix="amc-cmake.", dir=os.environ.get("TMPDIR", "/tmp"))
    print(f"  [INFO] BUILD_DIR: {build_dir}")

    try:
        # Step 1: cmake configure
        print("  [INFO] Configuring CMake fixture...")
        cmake_cmd = [
            "cmake",
            "-S", os.path.join(amc_root, "amc/tests/cmake_fixture"),
            "-B", build_dir,
            "-G", "Ninja",
            f"-DAMC_MODULE_DIR={amc_root}/amc/cmake",
            f"-DAMC_FIXTURE_DIR={amc_root}/amc/tests/fixtures",
            f"-DAMC_EXECUTABLE={amc_bin}/amc",
        ]
        subprocess.run(cmake_cmd, check=True, capture_output=True, text=True)

        # Step 2: cmake build
        print("  [INFO] Building CMake target...")
        subprocess.run(
            ["cmake", "--build", build_dir, "--target", "amc_cmake_consumer"],
            check=True, capture_output=True, text=True,
        )

        # Step 3: verify artifacts
        abix_output = os.path.join(build_dir, "amc-output/build/amc_test.abix")
        hpp_output = os.path.join(build_dir, "generated/amc_metadata.hpp")

        passed = 0
        failed = 0

        if os.path.isfile(abix_output):
            print(f"  [PASS] {abix_output} exists")
            passed += 1
        else:
            print(f"  [FAIL] {abix_output} does not exist")
            failed += 1

        if os.path.isfile(hpp_output):
            print(f"  [PASS] {hpp_output} exists")
            passed += 1
        else:
            print(f"  [FAIL] {hpp_output} does not exist")
            failed += 1

        print(f"\n=== Results: {passed} passed, {failed} failed ===")
        sys.exit(failed)

    finally:
        shutil.rmtree(build_dir, ignore_errors=True)


if __name__ == "__main__":
    main()