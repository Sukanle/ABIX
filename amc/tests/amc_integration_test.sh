#!/usr/bin/env bash
set -euo pipefail

AMC_BIN="${1:?usage: amc_integration_test.sh <amc_bin_dir>}"
FIXTURES_DIR="$(cd "$(dirname "$0")" && pwd)/fixtures"
SOURCE_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/amc-integration.XXXXXX")
trap 'rm -rf "$BUILD_DIR"' EXIT
GEN_SEPARATE=$BUILD_DIR/autogen-separate
GEN_FULL=$BUILD_DIR/autogen-full
mkdir -p $GEN_SEPARATE
mkdir -p $GEN_FULL
FAILED=0
PASSED=0

info()  { echo "  [INFO] $*"; }
pass()  { PASSED=$((PASSED + 1)); echo "  [PASS] $*"; }
fail()  { FAILED=$((FAILED + 1)); echo "  [FAIL] $*"; }

# Ensure fixture files exist
# for f in amc_test_types.hpp amc_test.abic.toml compile_commands.json; do
#     if [ ! -f "$FIXTURES_DIR/$f" ]; then
#         echo "  [ERROR] Missing fixture: $FIXTURES_DIR/$f"
#         exit 1
#     fi
# done

echo "=== AMC Integration Test ==="
echo "  AMC_BIN:       $AMC_BIN"
echo "  FIXTURES_DIR:  $FIXTURES_DIR"
echo "  BUILD_DIR:      $BUILD_DIR"
echo "  GEN_SEPARATE: $GEN_SEPARATE"
echo "  GEN_FULL:       $GEN_FULL"
echo ""

# ════════════════════════════════════════════════════════════════
# Step 1-6: 分步 frontend → generate
# ════════════════════════════════════════════════════════════════
echo "[step 1] amc frontend -l cpp -c amc_test.abic.toml -o test.abix"
if "$AMC_BIN/amc" frontend -l cpp -c "$FIXTURES_DIR/amc_test.abic.toml" -o "$GEN_SEPARATE/test.abix" 2>&1; then
    pass "frontend succeeded"
else
    fail "frontend failed"
    echo "  Skipping remaining steps due to frontend failure"
    echo ""
    echo "=== Results: $PASSED passed, $FAILED failed ==="
    exit 1
fi

echo ""
echo "[step 2] amc validate test.abix"
if "$AMC_BIN/amc" validate "$GEN_SEPARATE/test.abix" 2>&1; then
    pass "validate succeeded"
else
    fail "validate failed"
fi

echo ""
echo "[step 3] amc inspect test.abix"
INSPECT_OUTPUT=$("$AMC_BIN/amc" inspect "$GEN_SEPARATE/test.abix" 2>&1) || true
info "inspect output: $INSPECT_OUTPUT"

if echo "$INSPECT_OUTPUT" | grep -q "types="; then
    pass "inspect shows types"
else
    fail "inspect missing types"
fi

echo ""
echo "[step 3b] amc-dump v4/M8 metadata"
DUMP_JSON="$BUILD_DIR/dump.json"
if "$AMC_BIN/amc-dump" "$GEN_SEPARATE/test.abix" --json "$DUMP_JSON" 2>&1; then
    pass "amc-dump JSON succeeded"
    DUMP_CONTENT=$(cat "$DUMP_JSON")
    for PATTERN in '"format_version": 4' '"layout_hash"' '"runtime_descriptor"' \
                   '"type_id_bits": 128' '"registry": "RuntimeRegistry"'; do
        if echo "$DUMP_CONTENT" | grep -q "$PATTERN"; then
            pass "amc-dump JSON contains: $PATTERN"
        else
            fail "amc-dump JSON missing: $PATTERN"
        fi
    done
else
    fail "amc-dump JSON failed"
fi

echo ""
echo "[step 4] amc generate test.abix -l cpp -o generated/"
if "$AMC_BIN/amc" generate "$GEN_SEPARATE/test.abix" -l cpp -o "$GEN_SEPARATE" 2>&1; then
    pass "generate succeeded"
else
    fail "backend failed"
fi

echo ""
echo "[step 5] verify generated header content"
if [ -f "$GEN_SEPARATE/amc_generated.hpp" ]; then
    pass "generated header exists"

    CONTENT=$(cat "$GEN_SEPARATE/amc_generated.hpp")

    for PATTERN in "AmcTestFoo_ABIX" "AmcTestBar_ABIX" "AmcTestColor_ABIX" \
                   "type_id_lo" "type_id_hi" "x_offset" "y_offset" \
                   "a_offset" "b_offset" "c_offset" \
                   "TypeTraits" "LayoutInfo" "FunctionInfo" "ModuleInfo" \
                   "namespace amc_generated" "#pragma once"; do
        if echo "$CONTENT" | grep -q "$PATTERN"; then
            pass "generated header contains: $PATTERN"
        else
            fail "generated header missing: $PATTERN"
        fi
    done
else
    fail "generated header does not exist"
fi

echo ""
echo "[step 6] verify generated header is valid C++17"
if command -v clang++ &>/dev/null; then
    if clang++ -std=c++17 -I"$SOURCE_ROOT" -fsyntax-only "$GEN_SEPARATE/amc_generated.hpp" 2>&1; then
        pass "generated header compiles as C++17"
    else
        fail "generated header fails C++17 compilation"
    fi
else
    info "clang++ not found, skipping C++17 syntax check"
fi

# ════════════════════════════════════════════════════════════════
# Step 7: 完整管线 amc build（使用 -B 指定构建目录）
# ════════════════════════════════════════════════════════════════
echo ""
echo "[step 7] amc build -B <build_dir> amc_test.abic.toml (full pipeline)"

if "$AMC_BIN/amc" build -B "$GEN_FULL" "$FIXTURES_DIR/amc_test.abic.toml" 2>&1; then
    pass "amc build succeeded"

    if [ -f "$GEN_FULL/build/amc_test.abix" ]; then
        pass "amc build produced configured .abix artifact"
    else
        fail "amc build did not produce configured .abix artifact"
    fi
if [ ! -f "$GEN_FULL/generated/amc_generated.hpp" ]; then
        pass "amc build does not generate language projection"
    else
        fail "amc build unexpectedly generated language projection"
    fi
else
    fail "amc build failed"
fi

echo ""
echo "[step 8] AMC-M8 semantic metadata"
M8_ABIX="$BUILD_DIR/m8.abix"
M8_DUMP="$BUILD_DIR/m8.dump"
if "$AMC_BIN/amc" frontend -l cpp -c "$FIXTURES_DIR/amc_m8.abic.toml" -o "$M8_ABIX" 2>&1 \
    && "$AMC_BIN/amc-dump" "$M8_ABIX" > "$M8_DUMP"; then
    pass "M8 fixture frontend and dump succeeded"
    for PATTERN in "amc_m8::IntBox kind=record" "amc_m8::Count kind=alias" \
                   "bits type=" "flags=50331649" "Base type=" \
                   "calling_convention=1"; do
        if grep -q "$PATTERN" "$M8_DUMP"; then
            pass "M8 metadata contains: $PATTERN"
        else
            fail "M8 metadata missing: $PATTERN"
        fi
    done
else
    fail "M8 fixture frontend or dump failed"
fi

echo ""
echo "[step 8b] M8 static MapPrivate"
MAP_V1="$BUILD_DIR/map-v1.abix"
MAP_V2="$BUILD_DIR/map-v2.abix"
MAP_REPORT="$BUILD_DIR/map-report.abix"
MAP_HPP="$BUILD_DIR/amc_map.hpp"
MAP_DUMP="$BUILD_DIR/map-diff.dump"
MAP_DUMP_JSON="$BUILD_DIR/map-diff.json"
if "$AMC_BIN/amc" frontend -l cpp -c "$FIXTURES_DIR/map_v1.abic.toml" -o "$MAP_V1" 2>&1 \
    && "$AMC_BIN/amc" frontend -l cpp -c "$FIXTURES_DIR/map_v2.abic.toml" -o "$MAP_V2" 2>&1 \
    && "$AMC_BIN/amc" diff "$MAP_V1" "$MAP_V2" -o "$MAP_REPORT" 2>&1 \
    && "$AMC_BIN/amc" generate "$MAP_REPORT" -l cpp -o "$MAP_HPP" 2>&1 \
    && clang++ -std=c++17 -I"$SOURCE_ROOT" -I"$BUILD_DIR" \
        "$SOURCE_ROOT/amc/tests/map_private_consumer.cpp" -o "$BUILD_DIR/map_private_consumer" \
    && "$BUILD_DIR/map_private_consumer" \
    && "$AMC_BIN/amc-dump" diff "$MAP_V1" "$MAP_V2" > "$MAP_DUMP" \
    && "$AMC_BIN/amc-dump" diff "$MAP_V1" "$MAP_V2" --json "$MAP_DUMP_JSON" \
    && grep -q "map_compatible" "$MAP_DUMP" \
    && grep -q '"compatibility"' "$MAP_DUMP_JSON"; then
    pass "MapPrivate matches Runtime Map copy/default semantics"
    pass "amc-dump diff reports compatibility and JSON"
else
    fail "MapPrivate generation, Runtime Map equivalence, or amc-dump diff failed"
fi

echo ""
echo "[step 9] AMC-M9 diff and compatibility commands"
M9_REPORT="$BUILD_DIR/m9-report.abix"
if "$AMC_BIN/amc" diff "$GEN_SEPARATE/test.abix" "$GEN_SEPARATE/test.abix" -o "$M9_REPORT" 2>&1 \
    && "$AMC_BIN/amc" validate "$M9_REPORT" 2>&1 \
    && "$AMC_BIN/amc" compatibility "$GEN_SEPARATE/test.abix" "$GEN_SEPARATE/test.abix" 2>&1; then
    pass "M9 diff report and compatibility command succeeded"
else
    fail "M9 diff report or compatibility command failed"
fi

echo ""
echo "[step 10] ABIX self-description bootstrap artifact"
SELF_BUILD="$BUILD_DIR/self-build"
SELF_ABIX="$SELF_BUILD/build/abix_self.abix"
SELF_HPP="$BUILD_DIR/abix_self_metadata.hpp"
if "$AMC_BIN/amc" build -c "$SOURCE_ROOT/abix/self/abix_self.abic.toml" -B "$SELF_BUILD" 2>&1 \
    && "$AMC_BIN/amc" generate "$SELF_ABIX" -l cpp -o "$SELF_HPP" 2>&1 \
    && "$AMC_BIN/amc" validate "$SELF_ABIX" 2>&1; then
    pass "ABIX self-description artifact generated and validated"
    SELF_INSPECT=$("$AMC_BIN/amc" inspect "$SELF_ABIX" 2>&1)
    if grep -q "skl_abix_runtime_TypeDescriptor_ABIX" "$SELF_HPP" \
        && grep -q "skl_abix_model_TypeDesc_ABIX" "$SELF_HPP"; then
        pass "self-description contains model and runtime descriptors"
    else
        fail "self-description is missing model or runtime descriptors"
    fi
    if command -v clang++ &>/dev/null \
        && clang++ -std=c++17 -I"$SOURCE_ROOT" -I"$BUILD_DIR" \
            "$SOURCE_ROOT/amc/tests/self_type_of_consumer.cpp" -o "$BUILD_DIR/self_type_of_consumer" \
        && "$BUILD_DIR/self_type_of_consumer"; then
        pass "native type_of, Registry lookup, and registered EBR metadata self-hosting succeeded"
    else
        fail "native type_of, Registry lookup, or registered EBR metadata self-hosting failed"
    fi
else
    fail "ABIX self-description generation failed"
fi

echo ""
echo "[step 11] AMC core self-description bootstrap artifact"
AMC_SELF_BUILD="$BUILD_DIR/amc-self-build"
AMC_SELF_ABIX="$AMC_SELF_BUILD/build/amc_core.abix"
AMC_SELF_HPP="$BUILD_DIR/amc_core.hpp"
if "$AMC_BIN/amc" build -c "$SOURCE_ROOT/amc/self.abic.toml" -B "$AMC_SELF_BUILD" 2>&1 \
    && "$AMC_BIN/amc" validate "$AMC_SELF_ABIX" 2>&1 \
    && "$AMC_BIN/amc" generate "$AMC_SELF_ABIX" -l cpp -o "$AMC_SELF_HPP" 2>&1; then
    pass "AMC core self-description artifact generated and validated"
    if grep -q "amc_AbiModule_ABIX" "$AMC_SELF_HPP" \
        && grep -q "amc_MapOperation_ABIX" "$AMC_SELF_HPP"; then
        pass "AMC self-description contains core IR descriptors"
    else
        fail "AMC self-description is missing core IR descriptors"
    fi
    if command -v clang++ &>/dev/null \
        && clang++ -std=c++17 -I"$SOURCE_ROOT" -I"$BUILD_DIR" \
            "$SOURCE_ROOT/amc/tests/amc_self_consumer.cpp" -o "$BUILD_DIR/amc_self_consumer" \
        && "$BUILD_DIR/amc_self_consumer"; then
        pass "AMC native type_of self-description consumer succeeded"
    else
        fail "AMC native type_of self-description consumer failed"
    fi
else
    fail "AMC core self-description generation failed"
fi

echo ""
echo "[step 10] AMC-M10 provider IPC and capabilities"
IPC_OUTPUT=$(printf '%s\n' '{"type":"INIT","protocol":1}' \
    '{"type":"QUERY_CAPABILITIES"}' '{"type":"DONE"}' | "$AMC_BIN/amc-cpp" --ipc)
if echo "$IPC_OUTPUT" | grep -q '"type":"READY"' \
    && echo "$IPC_OUTPUT" | grep -q '"type":"CAPABILITIES"' \
    && "$AMC_BIN/amc" --list-languages | grep -qx 'cpp' \
    && "$AMC_BIN/amc" --describe-language cpp | grep -q 'protocol=jsonl-v1'; then
    pass "M10 IPC handshake and capability discovery succeeded"
else
    fail "M10 IPC handshake or capability discovery failed"
fi

echo ""
echo "=== Results: $PASSED passed, $FAILED failed ==="
exit $FAILED
