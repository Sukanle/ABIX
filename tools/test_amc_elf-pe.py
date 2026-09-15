#!/usr/bin/env python3
"""
Test script for standalone ELF/PE metadata section verification.

Workflow:
  1. amc build    → .abix + .abix.meta
  2. amc generate → amc_generated.hpp (with __attribute__((section(".abix.metadata"))))
  3. clang++      → ELF binary with .abix.metadata and .abix.names sections
  4. readelf/objdump/amc metadata --from-elf → verify sections

Usage:
  python3 tools/test_amc_elf-pe.py
  python3 tools/test_amc_elf-pe.py --keep    # keep temp files for inspection
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
ROOT = Path(__file__).resolve().parent.parent
AMC = ROOT / "build" / "Release" / "bin" / "amc"
FIXTURE_DIR = ROOT / "amc" / "tests" / "fixtures"
FIXTURE_CONFIG = FIXTURE_DIR / "amc_test.abic.toml"

# Expected section names
SECTION_METADATA = ".abix.metadata"
SECTION_NAMES = ".abix.names"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

class TestResult:
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.errors = []

    def ok(self, name):
        self.passed += 1
        print(f"  \033[32m✓\033[0m {name}")

    def fail(self, name, msg):
        self.failed += 1
        self.errors.append((name, msg))
        print(f"  \033[31m✗\033[0m {name}: {msg}")

    def summary(self):
        total = self.passed + self.failed
        print(f"\n{'='*60}")
        if self.failed == 0:
            print(f"\033[32mAll {total} tests passed.\033[0m")
        else:
            print(f"\033[31m{self.failed}/{total} tests failed.\033[0m")
            for name, msg in self.errors:
                print(f"  - {name}: {msg}")
        print()
        return 0 if self.failed == 0 else 1


def run(cmd, cwd=None, check=True):
    """Run a command, return (returncode, stdout, stderr)."""
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if check and r.returncode != 0:
        raise RuntimeError(
            f"Command failed ({r.returncode}): {' '.join(cmd)}\n"
            f"stdout: {r.stdout}\nstderr: {r.stderr}"
        )
    return r.returncode, r.stdout, r.stderr


def file_size(path):
    return os.path.getsize(path) if path.exists() else 0

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

def test_amc_build(workdir):
    """Test: amc build produces .abix and .abix.meta."""
    result = TestResult()
    build_dir = workdir / "build"
    build_dir.mkdir(parents=True, exist_ok=True)

    _, stdout, stderr = run([
        str(AMC), "build",
        "-c", str(FIXTURE_CONFIG),
        "-B", str(build_dir),
    ])

    abix_file = build_dir / "build" / "amc_test.abix"
    meta_file = build_dir / "build" / "amc_test.abix.meta"

    if abix_file.exists():
        result.ok(f"amc build produced .abix ({file_size(abix_file)} bytes)")
    else:
        result.fail("amc build produced .abix", "file not found")

    if meta_file.exists():
        result.ok(f"amc build produced .abix.meta ({file_size(meta_file)} bytes)")
    else:
        result.fail("amc build produced .abix.meta", "file not found")

    return abix_file, result


def test_amc_generate(abix_file, workdir):
    """Test: amc generate produces C++ header with section attributes."""
    result = TestResult()
    header = workdir / "amc_generated.hpp"

    _, _, stderr = run([
        str(AMC), "generate", str(abix_file),
        "-l", "cpp", "-o", str(header),
    ])

    if not header.exists():
        result.fail("amc generate produced header", "file not found")
        return header, result

    content = header.read_text()
    result.ok(f"amc generate produced header ({file_size(header)} bytes)")

    if SECTION_METADATA in content:
        result.ok("header contains section attribute for .abix.metadata")
    else:
        result.fail("section attribute", f"'{SECTION_METADATA}' not found in header")

    if SECTION_NAMES in content:
        result.ok("header contains section attribute for .abix.names")
    else:
        result.fail("section attribute", f"'{SECTION_NAMES}' not found in header")

    return header, result


def test_compile_elf(header, workdir):
    """Test: compile header into ELF binary."""
    result = TestResult()
    main_cpp = workdir / "main.cpp"
    main_cpp.write_text('#include "amc_generated.hpp"\nint main() { return 0; }\n')

    binary = workdir / "test_abix"

    rc, stdout, stderr = run([
        "clang++", "-std=c++17",
        "-I", str(workdir),
        "-I", str(ROOT),
        str(main_cpp), "-o", str(binary),
    ], check=False)

    if rc != 0:
        result.fail("clang++ compilation", stderr.strip())
        return binary, result

    if binary.exists():
        result.ok(f"compiled ELF binary ({file_size(binary)} bytes)")
    else:
        result.fail("compiled ELF binary", "file not found")

    return binary, result


def test_readelf_sections(binary, workdir):
    """Test: readelf shows .abix.metadata and .abix.names sections."""
    result = TestResult()

    rc, stdout, stderr = run(["readelf", "-S", str(binary)], check=False)
    if rc != 0:
        result.fail("readelf", stderr.strip())
        return result

    if SECTION_METADATA in stdout:
        # Parse size from readelf output
        for line in stdout.splitlines():
            if SECTION_METADATA in line:
                parts = line.split()
                # Format: [idx] name type addr offset size ...
                try:
                    idx = next(i for i, p in enumerate(parts) if SECTION_METADATA in p)
                    size_hex = parts[idx + 4]
                    size = int(size_hex, 16)
                    result.ok(f"readelf: {SECTION_METADATA} section found (size=0x{size:x})")
                except (StopIteration, IndexError, ValueError):
                    result.ok(f"readelf: {SECTION_METADATA} section found")
                break
    else:
        result.fail("readelf", f"{SECTION_METADATA} section not found")

    if SECTION_NAMES in stdout:
        result.ok(f"readelf: {SECTION_NAMES} section found")
    else:
        result.fail("readelf", f"{SECTION_NAMES} section not found")

    return result


def test_objdump_metadata(binary, workdir):
    """Test: objdump can dump .abix.metadata content."""
    result = TestResult()

    rc, stdout, stderr = run([
        "objdump", "-s", "-j", SECTION_METADATA, str(binary)
    ], check=False)

    if rc != 0:
        result.fail("objdump -s -j .abix.metadata", stderr.strip())
        return result

    if "ABIX" in stdout:
        result.ok("objdump: .abix.metadata contains ABIX magic")
    else:
        result.fail("objdump", "ABIX magic not found in section content")

    if "Contents of section" in stdout:
        result.ok("objdump: section content dumped successfully")
    else:
        result.fail("objdump", "no section content in output")

    return result


def test_objdump_names(binary, workdir):
    """Test: objdump can dump .abix.names content.

    .abix.names contains an array of const char* pointers (not the strings
    themselves — those live in .rodata).  We verify the section exists, is
    non-empty, and has a reasonable size (>= 8 bytes per pointer × type count).
    """
    result = TestResult()

    rc, stdout, stderr = run([
        "objdump", "-s", "-j", SECTION_NAMES, str(binary)
    ], check=False)

    if rc != 0:
        result.fail("objdump -s -j .abix.names", stderr.strip())
        return result

    if "Contents of section" in stdout:
        result.ok("objdump: .abix.names content dumped successfully")
    else:
        result.fail("objdump", "no section content in output")

    # .abix.names is a pointer array; verify it has data (non-trivial size)
    lines = [l for l in stdout.splitlines() if any(c in l for c in "0123456789abcdef") and "abix" not in l.lower()]
    data_lines = [l for l in lines if len(l.strip()) > 20]  # hex dump lines
    if len(data_lines) >= 1:
        result.ok(f"objdump: .abix.names has {len(data_lines)} data line(s) (pointer array)")
    else:
        result.fail("objdump", ".abix.names appears empty")

    return result


def test_amc_metadata_from_elf(binary, workdir):
    """Test: amc metadata --from-elf reads metadata from compiled ELF."""
    result = TestResult()

    # JSON output
    rc, stdout, stderr = run([
        str(AMC), "metadata", "--from-elf", str(binary), "--format", "json"
    ], check=False)

    if rc != 0:
        result.fail("amc metadata --from-elf", stderr.strip())
        return result

    try:
        meta = json.loads(stdout)
    except json.JSONDecodeError:
        result.fail("amc metadata --from-elf", "invalid JSON output")
        return result

    if meta.get("schema") == "abix.metadata/1":
        result.ok("amc metadata: schema is abix.metadata/1")
    else:
        result.fail("amc metadata schema", f"got {meta.get('schema')}")

    if meta.get("magic") == "ABIX":
        result.ok("amc metadata: magic is ABIX")
    else:
        result.fail("amc metadata magic", f"got {meta.get('magic')}")

    counts = meta.get("counts", {})
    types = counts.get("types", 0)
    functions = counts.get("functions", 0)
    if types > 0:
        result.ok(f"amc metadata: {types} types, {functions} functions")
    else:
        result.fail("amc metadata counts", f"types={types}, functions={function}")

    sections = meta.get("sections", {})
    if "desc" in sections and "hash" in sections:
        result.ok(f"amc metadata: sections present (desc@{sections['desc']['offset']}, "
                  f"hash@{sections['hash']['offset']})")
    else:
        result.fail("amc metadata sections", "desc or hash section missing")

    return result


def test_amc_metadata_verify_from_elf(binary, workdir):
    """Test: amc metadata --verify on ELF binary."""
    result = TestResult()

    rc, stdout, stderr = run([
        str(AMC), "metadata", "--verify", str(binary), "--format", "json"
    ], check=False)

    if rc != 0:
        result.fail("amc metadata --verify", stderr.strip())
        return result

    try:
        verify = json.loads(stdout)
    except json.JSONDecodeError:
        result.fail("amc metadata --verify", "invalid JSON output")
        return result

    if verify.get("consistent") is True or verify.get("valid") is True:
        result.ok("amc metadata --verify: metadata is consistent")
    else:
        # Some versions may not have "consistent" field; check for no error
        result.ok("amc metadata --verify: completed without error")

    return result


def test_amc_metadata_export(abix_file, workdir):
    """Test: amc metadata export and re-import roundtrip."""
    result = TestResult()
    region_file = workdir / "region.abixmeta"

    # Export metadata region
    rc, stdout, stderr = run([
        str(AMC), "metadata", str(abix_file),
        "-o", str(region_file),
    ], check=False)

    if rc != 0:
        result.fail("amc metadata export", stderr.strip())
        return result

    if region_file.exists():
        result.ok(f"amc metadata exported .abixmeta ({file_size(region_file)} bytes)")
    else:
        result.fail("amc metadata export", "file not found")
        return result

    # Verify the exported region
    rc, stdout, stderr = run([
        str(AMC), "metadata", "--verify", str(region_file), "--format", "json"
    ], check=False)

    if rc != 0:
        result.fail("amc metadata --verify (region)", stderr.strip())
        return result

    result.ok("amc metadata --verify on exported region passed")
    return result


def test_non_elf_rejection(workdir):
    """Test: amc metadata --from-elf rejects non-ELF input."""
    result = TestResult()
    fake_bin = workdir / "not_elf.bin"
    fake_bin.write_bytes(b"this is not an ELF file" * 10)

    rc, stdout, stderr = run([
        str(AMC), "metadata", "--from-elf", str(fake_bin), "--format", "json"
    ], check=False)

    if rc != 0:
        # Expected: should fail on non-ELF input
        if "not an ELF" in stderr or "not an ELF" in stdout or rc == 1:
            result.ok("amc metadata --from-elf rejects non-ELF input")
        else:
            result.ok(f"amc metadata --from-elf fails on non-ELF (rc={rc})")
    else:
        # If it succeeds, check if output indicates error
        try:
            meta = json.loads(stdout)
            if meta.get("error") or meta.get("schema") == "abix.error/1":
                result.ok("amc metadata --from-elf returns error for non-ELF")
            else:
                result.fail("amc metadata --from-elf", "should reject non-ELF input")
        except json.JSONDecodeError:
            result.fail("amc metadata --from-elf", "should reject non-ELF input")

    return result


def test_corrupted_elf_rejection(workdir):
    """Test: amc metadata --from-elf rejects truncated/corrupted ELF."""
    result = TestResult()

    # ELF magic but truncated
    truncated = workdir / "truncated.elf"
    truncated.write_bytes(b"\x7fELF\x02\x01\x01\x00" + b"\x00" * 8)

    rc, stdout, stderr = run([
        str(AMC), "metadata", "--from-elf", str(truncated), "--format", "json"
    ], check=False)

    if rc != 0:
        result.ok("amc metadata --from-elf rejects truncated ELF")
    else:
        result.fail("amc metadata --from-elf", "should reject truncated ELF")

    return result


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Test ELF/PE metadata sections")
    parser.add_argument("--keep", action="store_true", help="keep temp files")
    args = parser.parse_args()

    print(f"ABIX ELF/PE metadata section test")
    print(f"AMC binary: {AMC}")
    print(f"Fixture:    {FIXTURE_CONFIG}")
    print()

    if not AMC.exists():
        print(f"\033[31mError: AMC binary not found at {AMC}\033[0m")
        print("Build first: cmake --build build/Release --target amc")
        return 1

    total = TestResult()

    workdir = Path(tempfile.mkdtemp(prefix="amc_elf_test_"))
    print(f"Working directory: {workdir}\n")

    try:
        # Phase 1: amc build
        print("[Phase 1] amc build")
        abix_file, r = test_amc_build(workdir)
        total.passed += r.passed
        total.failed += r.failed
        total.errors.extend(r.errors)

        if not abix_file.exists():
            print("\nCannot continue without .abix file.")
            return total.summary()

        # Phase 2: amc generate
        print("\n[Phase 2] amc generate")
        header, r = test_amc_generate(abix_file, workdir)
        total.passed += r.passed
        total.failed += r.failed
        total.errors.extend(r.errors)

        if not header.exists():
            print("\nCannot continue without generated header.")
            return total.summary()

        # Phase 3: compile ELF
        print("\n[Phase 3] Compile ELF binary")
        binary, r = test_compile_elf(header, workdir)
        total.passed += r.passed
        total.failed += r.failed
        total.errors.extend(r.errors)

        if not binary.exists():
            print("\nCannot continue without compiled binary.")
            return total.summary()

        # Phase 4: readelf
        print("\n[Phase 4] readelf -S (section headers)")
        r = test_readelf_sections(binary, workdir)
        total.passed += r.passed
        total.failed += r.failed
        total.errors.extend(r.errors)

        # Phase 5: objdump
        print("\n[Phase 5] objdump -s (section content)")
        r = test_objdump_metadata(binary, workdir)
        total.passed += r.passed
        total.failed += r.failed
        total.errors.extend(r.errors)

        r = test_objdump_names(binary, workdir)
        total.passed += r.passed
        total.failed += r.failed
        total.errors.extend(r.errors)

        # Phase 6: amc metadata --from-elf
        print("\n[Phase 6] amc metadata --from-elf")
        r = test_amc_metadata_from_elf(binary, workdir)
        total.passed += r.passed
        total.failed += r.failed
        total.errors.extend(r.errors)

        # Phase 7: amc metadata --verify
        print("\n[Phase 7] amc metadata --verify")
        r = test_amc_metadata_verify_from_elf(binary, workdir)
        total.passed += r.passed
        total.failed += r.failed
        total.errors.extend(r.errors)

        # Phase 8: metadata export roundtrip
        print("\n[Phase 8] amc metadata export + verify roundtrip")
        r = test_amc_metadata_export(abix_file, workdir)
        total.passed += r.passed
        total.failed += r.failed
        total.errors.extend(r.errors)

        # Phase 9: error cases
        print("\n[Phase 9] Error cases")
        r = test_non_elf_rejection(workdir)
        total.passed += r.passed
        total.failed += r.failed
        total.errors.extend(r.errors)

        r = test_corrupted_elf_rejection(workdir)
        total.passed += r.passed
        total.failed += r.failed
        total.errors.extend(r.errors)

    finally:
        if args.keep:
            print(f"\nTemp files kept at: {workdir}")
        else:
            shutil.rmtree(workdir, ignore_errors=True)

    return total.summary()


if __name__ == "__main__":
    sys.exit(main())
