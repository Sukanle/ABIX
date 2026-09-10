#!/usr/bin/env bash
# Verify AMC's metadata bootstrap loop for its own core IR.
#
# This is not compiler-source self-hosting: amc-cpp still uses Clang/LLVM to
# extract C++ semantics. It proves the compiled AMC stage can describe its
# core model, generate a valid .abix v4 artifact and C++ projection, and use
# that projection through RuntimeRegistry::type_of<T>().
#
# Usage:
#   tool/test_amc_bootstrap.sh
#   BUILD_DIR=/tmp/abix-build tool/test_amc_bootstrap.sh
#   AMC_BIN_DIR=/tmp/abix-build/bin tool/test_amc_bootstrap.sh
#   KEEP_ARTIFACTS=1 tool/test_amc_bootstrap.sh

set -euo pipefail

SOURCE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-/tmp/abix-amc-bootstrap-build}"
EXTERNAL_AMC_BIN_DIR="${AMC_BIN_DIR:-}"
AMC_BIN_DIR="${EXTERNAL_AMC_BIN_DIR:-$BUILD_DIR/bin}"
KEEP_ARTIFACTS="${KEEP_ARTIFACTS:-0}"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/amc-bootstrap.XXXXXX")"

cleanup() {
    if [[ "$KEEP_ARTIFACTS" == "1" ]]; then
        echo "[INFO] retained artifacts: $WORK_DIR"
    else
        rm -rf "$WORK_DIR"
    fi
}
trap cleanup EXIT

fail() {
    echo "[FAIL] $*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"
}

echo "=== AMC Core Metadata Bootstrap ==="
echo "[INFO] source root: $SOURCE_ROOT"
echo "[INFO] build dir:   $BUILD_DIR"
echo "[INFO] work dir:    $WORK_DIR"

require_command cmake
require_command clang++

if [[ -z "$EXTERNAL_AMC_BIN_DIR" ]]; then
    echo "[STEP] configure and build AMC"
    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        cmake -S "$SOURCE_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    fi
    cmake --build "$BUILD_DIR" --target amc amc-cpp -j2
elif [[ ! -x "$AMC_BIN_DIR/amc" || ! -x "$AMC_BIN_DIR/amc-cpp" ]]; then
    fail "AMC_BIN_DIR must contain executable amc and amc-cpp: $AMC_BIN_DIR"
fi

AMC="$AMC_BIN_DIR/amc"
[[ -x "$AMC" ]] || fail "AMC executable was not produced: $AMC"

ARTIFACT_BUILD="$WORK_DIR/build"
ARTIFACT="$ARTIFACT_BUILD/build/amc_core.abix"
HEADER="$WORK_DIR/amc_core.hpp"
CONSUMER="$WORK_DIR/amc_self_consumer"

echo "[STEP] extract AMC core metadata"
"$AMC" build -c "$SOURCE_ROOT/amc/self.abic.toml" -B "$ARTIFACT_BUILD"

[[ -f "$ARTIFACT" ]] || fail "expected artifact was not produced: $ARTIFACT"

echo "[STEP] validate and inspect artifact"
"$AMC" validate "$ARTIFACT"
INSPECT_OUTPUT="$("$AMC" inspect "$ARTIFACT")"
echo "[INFO] $INSPECT_OUTPUT"
[[ "$INSPECT_OUTPUT" == *"package=amc_core"* ]] || fail "artifact package is not amc_core"
[[ "$INSPECT_OUTPUT" == *"types="* && "$INSPECT_OUTPUT" != *"types=0"* ]] || fail "artifact contains no types"

echo "[STEP] generate C++ metadata projection"
"$AMC" generate "$ARTIFACT" -l cpp -o "$HEADER"
[[ -f "$HEADER" ]] || fail "expected generated header was not produced: $HEADER"
grep -q "amc_AbiModule_ABIX" "$HEADER" || fail "projection lacks AbiModule descriptor"
grep -q "amc_MapOperation_ABIX" "$HEADER" || fail "projection lacks MapOperation descriptor"

echo "[STEP] compile and run native type_of<T>() consumer"
clang++ -std=c++17 -I"$SOURCE_ROOT" -I"$WORK_DIR" \
    "$SOURCE_ROOT/amc/tests/amc_self_consumer.cpp" -o "$CONSUMER"
"$CONSUMER"

echo "[PASS] AMC core metadata bootstrap succeeded"
echo "[PASS] verified build -> validate -> generate -> RuntimeRegistry::type_of<T>()"
