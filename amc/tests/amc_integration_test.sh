#!/usr/bin/env bash
set -euo pipefail

AMC_BIN="${1:?usage: amc_integration_test.sh <amc_bin_dir>}"
FIXTURES_DIR="$(cd "$(dirname "$0")" && pwd)/fixtures"
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
    if clang++ -std=c++17 -fsyntax-only "$GEN_SEPARATE/amc_generated.hpp" 2>&1; then
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
echo "=== Results: $PASSED passed, $FAILED failed ==="
exit $FAILED
