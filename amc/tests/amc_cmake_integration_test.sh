#!/usr/bin/env bash
set -euo pipefail

AMC_BIN="${1:?usage: amc_cmake_integration_test.sh <amc_bin_dir>}"
AMC_ROOT="${2:?usage: amc_cmake_integration_test.sh <amc_bin_dir> <source_root>}"
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/amc-cmake.XXXXXX")
trap 'rm -rf "$BUILD_DIR"' EXIT

cmake -S "$AMC_ROOT/amc/tests/cmake_fixture" -B "$BUILD_DIR" -G Ninja \
  -DAMC_MODULE_DIR="$AMC_ROOT/amc/cmake" \
  -DAMC_FIXTURE_DIR="$AMC_ROOT/amc/tests/fixtures" \
  -DAMC_EXECUTABLE="$AMC_BIN/amc"
cmake --build "$BUILD_DIR" --target amc_cmake_consumer

test -f "$BUILD_DIR/amc-output/build/amc_test.abix"
test -f "$BUILD_DIR/generated/amc_metadata.hpp"
