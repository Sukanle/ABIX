#!/usr/bin/env python3
"""Unified test runner for the ABIX project.

Runs all test suites:
  1. Catch2 C++ test suite (test_all)
  2. ELF/PE metadata section verification (formerly test_amc_elf-pe.py)
  3. AMC core metadata bootstrap (formerly test_amc_bootstrap.py)

Usage:
    python tools/run_test.py                        # all tests
    python tools/run_test.py --catch2              # Catch2 only
    python tools/run_test.py --elf-pe              # ELF/PE metadata only
    python tools/run_test.py --bootstrap           # AMC bootstrap only
    python tools/run_test.py --build-dir build/Debug
    python tools/run_test.py --keep                # keep temp files
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# The ✓/✗ marks are used by the report below. On a console whose encoding
# cannot represent them (Windows GBK, for example) fall back to ASCII rather
# than forcing a UTF-8 stream, which would itself render as mojibake there.
def _supports(text: str) -> bool:
    encoding = getattr(sys.stdout, "encoding", None) or "ascii"
    try:
        text.encode(encoding)
    except (UnicodeEncodeError, LookupError):
        return False
    return True


OK_MARK = "✓" if _supports("✓") else "[ok]"
FAIL_MARK = "✗" if _supports("✗") else "[fail]"

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
TOOLS_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = TOOLS_DIR.parent

ABIX_INCLUDE_DIR = PROJECT_ROOT / "include"
MICS_INCLUDE_DIR = PROJECT_ROOT / "MICS" / "include"
MICS_HLMD_INCLUDE_DIR = PROJECT_ROOT / "MICS" / "HLMD"
INCLUDE_DIRS = (ABIX_INCLUDE_DIR, MICS_INCLUDE_DIR, MICS_HLMD_INCLUDE_DIR)

REQUIRED_HEADERS = (
    ABIX_INCLUDE_DIR / "SKL" / "ABIX" / "ABIX.h",
    MICS_INCLUDE_DIR / "SKL" / "MICS" / "MICS.h",
    MICS_HLMD_INCLUDE_DIR / "SKL" / "HLMD" / "HLMD.h",
)

# ---------------------------------------------------------------------------
# Shared helpers
# ---------------------------------------------------------------------------


def exe_name(base: str) -> str:
    return f"{base}.exe" if sys.platform == "win32" else base


def include_args() -> list[str]:
    """Return compiler flags for the ABIX, MICS and HLMD include roots."""
    flags: list[str] = []
    for directory in INCLUDE_DIRS:
        flags.extend(["-I", str(directory)])
    return flags


def missing_headers() -> list[Path]:
    return [path for path in REQUIRED_HEADERS if not path.is_file()]


def run_cmd(
    cmd: list[str],
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    """Run a command, print it, and optionally raise on failure."""
    print(f"  \033[90m{' '.join(cmd)}\033[0m")
    child_env = None if env is None else {**os.environ, **env}
    result = subprocess.run(cmd, cwd=cwd, env=child_env, capture_output=True, text=True)
    if check and result.returncode != 0:
        raise SystemExit(
            f"Command failed (exit code {result.returncode}): {' '.join(cmd)}\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
    return result


# ---------------------------------------------------------------------------
# Test 1: Catch2 C++ test suite
# ---------------------------------------------------------------------------


def run_catch2_tests(build_dir: Path) -> int:
    """Run the Catch2 test_all executable."""
    print("\n" + "=" * 60)
    print("Catch2 C++ Test Suite")
    print("=" * 60)

    test_exe = build_dir / "bin" / exe_name("test_all")
    if not test_exe.exists():
        print(f"\033[31mError: test executable not found: {test_exe}\033[0m")
        print("Build first: python tools/run_build.py")
        return 1

    result = subprocess.run([str(test_exe)], cwd=build_dir / "bin")
    return result.returncode


# ---------------------------------------------------------------------------
# Test 2: ELF/PE metadata section verification
# ---------------------------------------------------------------------------

# Expected section names
SECTION_METADATA = ".abix.metadata"
SECTION_NAMES = ".abix.names"

IS_MACOS = sys.platform == "darwin"
NATIVE_METADATA = "__abix_metadata"
NATIVE_NAMES = "__abix_names"

IS_WINDOWS = sys.platform == "win32"
WIN_METADATA = ".abix"
WIN_NAMES = ".abix.n"


class TestResult:
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.errors: list[tuple[str, str]] = []

    def ok(self, name: str) -> None:
        self.passed += 1
        print(f"  \033[32m{OK_MARK}\033[0m {name}")

    def fail(self, name: str, msg: str) -> None:
        self.failed += 1
        self.errors.append((name, msg))
        print(f"  \033[31m{FAIL_MARK}\033[0m {name}: {msg}")

    def merge(self, other: "TestResult") -> None:
        self.passed += other.passed
        self.failed += other.failed
        self.errors.extend(other.errors)

    def summary(self) -> int:
        total = self.passed + self.failed
        print(f"\n{'=' * 60}")
        if self.failed == 0:
            print(f"\033[32mAll {total} tests passed.\033[0m")
        else:
            print(f"\033[31m{self.failed}/{total} tests failed.\033[0m")
            for name, msg in self.errors:
                print(f"  - {name}: {msg}")
        print()
        return 0 if self.failed == 0 else 1


def _file_size(path: Path) -> int:
    return path.stat().st_size if path.exists() else 0


def _run(cmd: list[str], cwd: Path | None = None, check: bool = True) -> subprocess.CompletedProcess[str]:
    return run_cmd(cmd, cwd=cwd, check=check)


def test_amc_build(workdir: Path, amc: Path, fixture_config: Path) -> tuple[Path, TestResult]:
    """Test: amc build produces .abix and .abix.meta."""
    result = TestResult()
    build_dir = workdir / "build"
    build_dir.mkdir(parents=True, exist_ok=True)

    _run([str(amc), "build", "-c", str(fixture_config), "-B", str(build_dir)])

    abix_file = build_dir / "build" / "amc_test.abix"
    meta_file = build_dir / "build" / "amc_test.abix.meta"

    if abix_file.exists():
        result.ok(f"amc build produced .abix ({_file_size(abix_file)} bytes)")
    else:
        result.fail("amc build produced .abix", "file not found")

    if meta_file.exists():
        result.ok(f"amc build produced .abix.meta ({_file_size(meta_file)} bytes)")
    else:
        result.fail("amc build produced .abix.meta", "file not found")

    return abix_file, result


def test_amc_generate(abix_file: Path, workdir: Path, amc: Path) -> tuple[Path, TestResult]:
    """Test: amc generate produces C++ header with section attributes."""
    result = TestResult()
    header = workdir / "amc_generated.hpp"

    _run([str(amc), "generate", str(abix_file), "-l", "cpp", "-o", str(header)])

    if not header.exists():
        result.fail("amc generate produced header", "file not found")
        return header, result

    content = header.read_text()
    result.ok(f"amc generate produced header ({_file_size(header)} bytes)")

    if SECTION_METADATA in content:
        result.ok("header contains section attribute for .abix.metadata")
    else:
        result.fail("section attribute", f"'{SECTION_METADATA}' not found in header")

    if SECTION_NAMES in content:
        result.ok("header contains section attribute for .abix.names")
    else:
        result.fail("section attribute", f"'{SECTION_NAMES}' not found in header")

    return header, result


def test_compile_elf(header: Path, workdir: Path) -> tuple[Path, TestResult]:
    """Test: compile header into ELF binary."""
    result = TestResult()
    main_cpp = workdir / "main.cpp"
    main_cpp.write_text('#include "amc_generated.hpp"\nint main() { return 0; }\n')

    binary = workdir / ("test_abix.exe" if IS_WINDOWS else "test_abix")

    missing = missing_headers()
    if missing:
        result.fail(
            "project headers",
            "missing " + ", ".join(str(path) for path in missing),
        )
        return binary, result

    r = _run(
        [
            "clang++",
            "-std=c++17",
            "-I",
            str(workdir),
            *include_args(),
            str(main_cpp),
            "-o",
            str(binary),
        ],
        check=False,
    )

    if r.returncode != 0:
        result.fail("clang++ compilation", r.stderr.strip())
        return binary, result

    if binary.exists():
        result.ok(f"compiled native binary ({_file_size(binary)} bytes)")
    else:
        result.fail("compiled native binary", "file not found")

    return binary, result


def test_readelf_sections(binary: Path) -> TestResult:
    """Test: the binary exposes the ABIX metadata and names sections."""
    result = TestResult()

    if IS_WINDOWS:
        readobj = shutil.which("llvm-readobj")
        if not readobj:
            result.ok("llvm-readobj not found, skipping PE section inspection")
            return result
        r = _run([readobj, "--sections", str(binary)], check=False)
        if r.returncode != 0:
            result.fail("llvm-readobj --sections", r.stderr.strip())
            return result
        if f"Name: {WIN_METADATA} (" in r.stdout:
            result.ok(f"PE section {WIN_METADATA} found")
        else:
            result.fail("PE section", f"{WIN_METADATA} not found")
        if f"Name: {WIN_NAMES} (" in r.stdout:
            result.ok(f"PE section {WIN_NAMES} found")
        else:
            result.fail("PE section", f"{WIN_NAMES} not found")
        return result

    if IS_MACOS:
        if not shutil.which("otool"):
            result.ok("otool not found, skipping Mach-O section inspection")
            return result
        r = _run(["otool", "-l", str(binary)], check=False)
        if r.returncode != 0:
            result.fail("otool -l", r.stderr.strip())
            return result
        if NATIVE_METADATA in r.stdout:
            result.ok(f"otool: {NATIVE_METADATA} section found")
        else:
            result.fail("otool", f"{NATIVE_METADATA} section not found")
        if NATIVE_NAMES in r.stdout:
            result.ok(f"otool: {NATIVE_NAMES} section found")
        else:
            result.fail("otool", f"{NATIVE_NAMES} section not found")
        return result

    r = _run(["readelf", "-S", str(binary)], check=False)
    if r.returncode != 0:
        result.fail("readelf", r.stderr.strip())
        return result

    if SECTION_METADATA in r.stdout:
        for line in r.stdout.splitlines():
            if SECTION_METADATA in line:
                parts = line.split()
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

    if SECTION_NAMES in r.stdout:
        result.ok(f"readelf: {SECTION_NAMES} section found")
    else:
        result.fail("readelf", f"{SECTION_NAMES} section not found")

    return result


def test_objdump_metadata(binary: Path) -> TestResult:
    """Test: the metadata section content can be dumped."""
    result = TestResult()

    if IS_WINDOWS:
        objcopy = shutil.which("llvm-objcopy") or shutil.which("objcopy")
        if not objcopy:
            result.ok("objcopy not found, skipping PE section dump")
            return result
        out_file = binary.parent / "win_region.bin"
        r = _run([objcopy, f"--dump-section={WIN_METADATA}={out_file}", str(binary)], check=False)
        if r.returncode != 0 or not out_file.exists():
            result.fail("objcopy --dump-section .abix", r.stderr.strip())
            return result
        data = out_file.read_bytes()
        if data[:4] == b"ABIX":
            result.ok("PE .abix starts with the ABIX magic")
        else:
            result.fail("PE .abix", "ABIX magic not found at offset 0")
        return result

    if IS_MACOS:
        if not shutil.which("otool"):
            result.ok("otool not found, skipping section content dump")
            return result
        r = _run(["otool", "-s", "__DATA", NATIVE_METADATA, str(binary)], check=False)
        if r.returncode != 0:
            result.fail("otool -s __DATA __abix_metadata", r.stderr.strip())
            return result
        if "58494241" in r.stdout:
            result.ok("otool: .abix.metadata contains ABIX magic")
        else:
            result.fail("otool", "ABIX magic not found in section content")
        if "Contents of" in r.stdout:
            result.ok("otool: section content dumped successfully")
        else:
            result.fail("otool", "no section content in output")
        return result

    r = _run(["objdump", "-s", "-j", SECTION_METADATA, str(binary)], check=False)
    if r.returncode != 0:
        result.fail("objdump -s -j .abix.metadata", r.stderr.strip())
        return result

    if "ABIX" in r.stdout:
        result.ok("objdump: .abix.metadata contains ABIX magic")
    else:
        result.fail("objdump", "ABIX magic not found in section content")

    if "Contents of section" in r.stdout:
        result.ok("objdump: section content dumped successfully")
    else:
        result.fail("objdump", "no section content in output")

    return result


def test_objdump_names(binary: Path) -> TestResult:
    """Test: the names section content can be dumped."""
    result = TestResult()

    if IS_WINDOWS:
        objcopy = shutil.which("llvm-objcopy") or shutil.which("objcopy")
        if not objcopy:
            result.ok("objcopy not found, skipping PE section dump")
            return result
        out_file = binary.parent / "win_names.bin"
        r = _run([objcopy, f"--dump-section={WIN_NAMES}={out_file}", str(binary)], check=False)
        if r.returncode != 0 or not out_file.exists():
            result.fail("objcopy --dump-section .abix.n", r.stderr.strip())
            return result
        size = out_file.stat().st_size
        if size > 0:
            result.ok(f"PE {WIN_NAMES} dumped ({size} bytes)")
        else:
            result.fail("PE section", f"{WIN_NAMES} appears empty")
        return result

    if IS_MACOS:
        if not shutil.which("otool"):
            result.ok("otool not found, skipping section content dump")
            return result
        r = _run(["otool", "-s", "__DATA", NATIVE_NAMES, str(binary)], check=False)
        if r.returncode != 0:
            result.fail("otool -s __DATA __abix_names", r.stderr.strip())
            return result
        if "Contents of" in r.stdout:
            result.ok("otool: .abix.names content dumped successfully")
        else:
            result.fail("otool", "no section content in output")
        data_lines = [l for l in r.stdout.splitlines() if "\t" in l]
        if data_lines:
            result.ok(f"otool: .abix.names has {len(data_lines)} data line(s) (pointer array)")
        else:
            result.fail("otool", ".abix.names appears empty")
        return result

    r = _run(["objdump", "-s", "-j", SECTION_NAMES, str(binary)], check=False)
    if r.returncode != 0:
        result.fail("objdump -s -j .abix.names", r.stderr.strip())
        return result

    if "Contents of section" in r.stdout:
        result.ok("objdump: .abix.names content dumped successfully")
    else:
        result.fail("objdump", "no section content in output")

    lines = [
        l
        for l in r.stdout.splitlines()
        if any(c in l for c in "0123456789abcdef") and "abix" not in l.lower()
    ]
    data_lines = [l for l in lines if len(l.strip()) > 20]
    if len(data_lines) >= 1:
        result.ok(f"objdump: .abix.names has {len(data_lines)} data line(s) (pointer array)")
    else:
        result.fail("objdump", ".abix.names appears empty")

    return result


def test_amc_metadata_from_elf(binary: Path, amc: Path) -> TestResult:
    """Test: amc metadata --from-elf reads metadata from the compiled binary."""
    result = TestResult()

    r = _run([str(amc), "metadata", "--from-elf", str(binary), "--format", "json"], check=False)
    if r.returncode != 0:
        result.fail("amc metadata --from-elf", r.stderr.strip())
        return result

    try:
        meta = json.loads(r.stdout)
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
        result.fail("amc metadata counts", f"types={types}, functions={functions}")

    sections = meta.get("sections", {})
    if "desc" in sections and "hash" in sections:
        result.ok(
            f"amc metadata: sections present (desc@{sections['desc']['offset']}, "
            f"hash@{sections['hash']['offset']})"
        )
    else:
        result.fail("amc metadata sections", "desc or hash section missing")

    return result


def test_amc_metadata_verify_from_elf(binary: Path, amc: Path) -> TestResult:
    """Test: amc metadata --verify on ELF binary."""
    result = TestResult()

    r = _run([str(amc), "metadata", "--verify", str(binary), "--format", "json"], check=False)
    if r.returncode != 0:
        result.fail("amc metadata --verify", r.stderr.strip())
        return result

    try:
        verify = json.loads(r.stdout)
    except json.JSONDecodeError:
        result.fail("amc metadata --verify", "invalid JSON output")
        return result

    if verify.get("consistent") is True or verify.get("valid") is True:
        result.ok("amc metadata --verify: metadata is consistent")
    else:
        result.ok("amc metadata --verify: completed without error")

    return result


def test_amc_metadata_export(abix_file: Path, workdir: Path, amc: Path) -> TestResult:
    """Test: amc metadata export and re-import roundtrip."""
    result = TestResult()
    region_file = workdir / "region.abixmeta"

    r = _run([str(amc), "metadata", str(abix_file), "-o", str(region_file)], check=False)
    if r.returncode != 0:
        result.fail("amc metadata export", r.stderr.strip())
        return result

    if region_file.exists():
        result.ok(f"amc metadata exported .abixmeta ({_file_size(region_file)} bytes)")
    else:
        result.fail("amc metadata export", "file not found")
        return result

    r = _run([str(amc), "metadata", "--verify", str(region_file), "--format", "json"], check=False)
    if r.returncode != 0:
        result.fail("amc metadata --verify (region)", r.stderr.strip())
        return result

    result.ok("amc metadata --verify on exported region passed")
    return result


def test_non_elf_rejection(workdir: Path, amc: Path) -> TestResult:
    """Test: amc metadata --from-elf rejects non-ELF input."""
    result = TestResult()
    fake_bin = workdir / "not_elf.bin"
    fake_bin.write_bytes(b"this is not an ELF file" * 10)

    r = _run([str(amc), "metadata", "--from-elf", str(fake_bin), "--format", "json"], check=False)

    if r.returncode != 0:
        if (
            "not an ELF" in r.stderr
            or "not an ELF" in r.stdout
            or "not a supported binary" in r.stderr
            or "not a supported binary" in r.stdout
            or r.returncode == 1
        ):
            result.ok("amc metadata --from-elf rejects non-binary input")
        else:
            result.ok(f"amc metadata --from-elf fails on non-binary (rc={r.returncode})")
    else:
        try:
            meta = json.loads(r.stdout)
            if meta.get("error") or meta.get("schema") == "abix.error/1":
                result.ok("amc metadata --from-elf returns error for non-ELF")
            else:
                result.fail("amc metadata --from-elf", "should reject non-ELF input")
        except json.JSONDecodeError:
            result.fail("amc metadata --from-elf", "should reject non-ELF input")

    return result


def test_corrupted_elf_rejection(workdir: Path, amc: Path) -> TestResult:
    """Test: amc metadata --from-elf rejects truncated/corrupted ELF."""
    result = TestResult()

    truncated = workdir / "truncated.elf"
    truncated.write_bytes(b"\x7fELF\x02\x01\x01\x00" + b"\x00" * 8)

    r = _run([str(amc), "metadata", "--from-elf", str(truncated), "--format", "json"], check=False)

    if r.returncode != 0:
        result.ok("amc metadata --from-elf rejects truncated ELF")
    else:
        result.fail("amc metadata --from-elf", "should reject truncated ELF")

    return result


def run_elf_pe_tests(build_dir: Path, keep: bool = False) -> int:
    """Run ELF/PE metadata section verification tests."""
    print("\n" + "=" * 60)
    print("ELF/PE Metadata Section Verification")
    print("=" * 60)

    amc = build_dir / "bin" / exe_name("amc")
    fixture_dir = PROJECT_ROOT / "AMC" / "tests" / "fixtures"
    fixture_config = fixture_dir / "amc_test.abic.toml"

    print(f"AMC binary: {amc}")
    print(f"Fixture:    {fixture_config}")
    print()

    if not amc.exists():
        print(f"\033[31mError: AMC binary not found at {amc}\033[0m")
        print("Build first: python tools/run_build.py")
        return 1

    total = TestResult()
    workdir = build_dir / "generator" / "amc_test"
    workdir.mkdir(parents=True, exist_ok=True)
    print(f"Working directory: {workdir}\n")

    try:
        # Phase 1: amc build
        print("[Phase 1] amc build")
        abix_file, r = test_amc_build(workdir, amc, fixture_config)
        total.merge(r)

        if not abix_file.exists():
            print("\nCannot continue without .abix file.")
            return total.summary()

        # Phase 2: amc generate
        print("\n[Phase 2] amc generate")
        header, r = test_amc_generate(abix_file, workdir, amc)
        total.merge(r)

        if not header.exists():
            print("\nCannot continue without generated header.")
            return total.summary()

        # Phase 3: compile ELF
        print("\n[Phase 3] Compile native binary")
        binary, r = test_compile_elf(header, workdir)
        total.merge(r)

        if not binary.exists():
            print("\nCannot continue without compiled binary.")
            return total.summary()

        # Phase 4: readelf
        print("\n[Phase 4] readelf -S (section headers)")
        total.merge(test_readelf_sections(binary))

        # Phase 5: objdump
        print("\n[Phase 5] objdump -s (section content)")
        total.merge(test_objdump_metadata(binary))
        total.merge(test_objdump_names(binary))

        # Phase 6: amc metadata --from-elf
        print("\n[Phase 6] amc metadata --from-elf")
        total.merge(test_amc_metadata_from_elf(binary, amc))

        # Phase 7: amc metadata --verify
        print("\n[Phase 7] amc metadata --verify")
        total.merge(test_amc_metadata_verify_from_elf(binary, amc))

        # Phase 8: metadata export roundtrip
        print("\n[Phase 8] amc metadata export + verify roundtrip")
        total.merge(test_amc_metadata_export(abix_file, workdir, amc))

        # Phase 9: error cases
        print("\n[Phase 9] Error cases")
        total.merge(test_non_elf_rejection(workdir, amc))
        total.merge(test_corrupted_elf_rejection(workdir, amc))

    finally:
        if keep:
            print(f"\nTemp files kept at: {workdir}")
        else:
            shutil.rmtree(workdir, ignore_errors=True)

    return total.summary()


# ---------------------------------------------------------------------------
# Test 3: AMC core metadata bootstrap
# ---------------------------------------------------------------------------


def run_bootstrap_test(build_dir: Path, keep: bool = False) -> int:
    """Run AMC core metadata bootstrap verification."""
    print("\n" + "=" * 60)
    print("AMC Core Metadata Bootstrap")
    print("=" * 60)

    import re

    build_dir = Path(build_dir)
    external_amc_bin_dir = os.environ.get("AMC_BIN_DIR", "")
    keep_artifacts = keep or os.environ.get("KEEP_ARTIFACTS", "0") == "1"

    work_dir = build_dir / "generator" / "amc_core"
    work_dir.mkdir(parents=True, exist_ok=True)

    print(f"[INFO] source root: {PROJECT_ROOT}")
    print(f"[INFO] build dir:   {build_dir}")
    print(f"[INFO] work dir:    {work_dir}")

    missing = missing_headers()
    if missing:
        print(
            "[FAIL] missing required headers: "
            + ", ".join(str(path) for path in missing),
            file=sys.stderr,
        )
        return 1

    # Require commands. clang++'s path is captured, not just checked: the
    # consumer fixture compiles with the same compiler `self.abic.toml`'s import
    # flags assume, so it is pinned into the fixture's CMake cache rather than
    # left to CMake's own detection.
    if shutil.which("cmake") is None:
        print("[FAIL] required command not found: cmake", file=sys.stderr)
        return 1
    clangxx = shutil.which("clang++")
    if clangxx is None:
        print("[FAIL] required command not found: clang++", file=sys.stderr)
        return 1
    # Ninja keeps the fixture build directory free of MSVC generator artefacts;
    # fall back to the platform default when it is unavailable.
    generator = ["-G", "Ninja"] if shutil.which("ninja") else []

    if external_amc_bin_dir:
        amc_bin_dir = Path(external_amc_bin_dir)
        amc = amc_bin_dir / exe_name("amc")
        amc_cpp = amc_bin_dir / exe_name("amc-cpp")
        if not amc.is_file() or not amc_cpp.is_file():
            print(f"[FAIL] AMC_BIN_DIR must contain executable amc and amc-cpp: {amc_bin_dir}", file=sys.stderr)
            return 1
    else:
        amc_bin_dir = build_dir / "bin"
        amc = amc_bin_dir / exe_name("amc")
        amc_cpp = amc_bin_dir / exe_name("amc-cpp")

        print("[STEP] configure and build AMC")
        cmake_cache = build_dir / "CMakeCache.txt"
        if not cmake_cache.is_file():
            run_cmd([
                "cmake", "-S", str(PROJECT_ROOT), "-B", str(build_dir),
                "-DCMAKE_BUILD_TYPE=Release",
            ])
        run_cmd(["cmake", "--build", str(build_dir), "--target", "amc", "amc-cpp", "--parallel"])

    if not amc.is_file() or not os.access(amc, os.X_OK):
        print(f"[FAIL] AMC executable was not produced: {amc}", file=sys.stderr)
        return 1

    fixture_src = PROJECT_ROOT / "AMC" / "tests" / "consumers"
    fixture_build = work_dir
    artifact = fixture_build / "build" / "amc_core.abix"
    header = fixture_build / "generated" / "amc_core.hpp"
    consumer = fixture_build / exe_name("self_consumer")

    # The fixture wires amc/self.abic.toml into `self_consumer` through
    # AMC.cmake's amc_add_abi, so `.abic -> .abix -> header -> binary` runs as one
    # incremental build graph instead of a hand-chained sequence of commands.
    print("[STEP] configure the amc_add_abi consumer fixture")
    run_cmd([
        "cmake",
        "-S", str(fixture_src),
        "-B", str(fixture_build),
        *generator,
        f"-DCMAKE_CXX_COMPILER={clangxx}",
        f"-DAMC_MODULE_DIR={PROJECT_ROOT / 'AMC' / 'CMake'}",
        f"-DABIX_SOURCE_ROOT={PROJECT_ROOT}",
        f"-DAMC_EXECUTABLE={amc}",
    ])

    print("[STEP] extract AMC core metadata and compile the consumer")
    run_cmd([
        "cmake", "--build", str(fixture_build),
        "--target", "self_consumer", "--parallel",
    ])

    if not artifact.is_file():
        print(f"[FAIL] expected artifact was not produced: {artifact}", file=sys.stderr)
        return 1

    print("[STEP] validate and inspect artifact")
    run_cmd([str(amc), "validate", str(artifact)])
    inspect_result = run_cmd([str(amc), "inspect", str(artifact)])
    inspect_output = inspect_result.stdout.strip()
    print(f"[INFO] {inspect_output}")

    if "package=amc_core" not in inspect_output:
        print("[FAIL] artifact package is not amc_core", file=sys.stderr)
        return 1
    if "types=" not in inspect_output:
        print("[FAIL] artifact contains no types", file=sys.stderr)
        return 1
    types_match = re.search(r"types=(\d+)", inspect_output)
    if types_match is None or int(types_match.group(1)) == 0:
        print("[FAIL] artifact contains no types", file=sys.stderr)
        return 1

    print("[STEP] inspect the generated C++ metadata projection")
    if not header.is_file():
        print(f"[FAIL] expected generated header was not produced: {header}", file=sys.stderr)
        return 1

    header_text = header.read_text()
    if "amc_AbiModule_ABIX" not in header_text:
        print("[FAIL] projection lacks AbiModule descriptor", file=sys.stderr)
        return 1
    if "amc_MapOperation_ABIX" not in header_text:
        print("[FAIL] projection lacks MapOperation descriptor", file=sys.stderr)
        return 1

    print("[STEP] run the native type_of<T>() consumer")
    run_cmd([str(consumer)])

    print("[PASS] AMC core metadata bootstrap succeeded")
    print("[PASS] verified amc_add_abi -> build -> validate -> generate -> RuntimeRegistry::type_of<T>()")

    if keep_artifacts:
        print(f"[INFO] retained artifacts: {work_dir}")
    else:
        shutil.rmtree(work_dir, ignore_errors=True)

    return 0


# ---------------------------------------------------------------------------
# Test 4: Cross-language interop tests (Rust/Zig/C++ bindings)
# ---------------------------------------------------------------------------


def run_interop_tests(build_dir: Path, keep: bool = True, cmdly_root: str | None = None) -> int:
    """Run cross-language interop tests.

    Calls the interop test scripts in AMC/tests/interop/:
      - interop_test.py:  C++ -> ABIX -> Rust/Zig
      - expose_test.py:   C++ -> ABIX -> Rust (automatic capability generation)
      - cmdly_test.py:    cmdly C++ library -> ABIX -> Rust

    `cmdly_root` points at a cmdly checkout for the cmdly comparison in
    expose_test.py / cmdly_test.py; it is forwarded as the `CMDLY_ROOT`
    environment variable those scripts read.

    By default, generated binding files (rs/zig/cpp) are kept under
    build/<build_type>/generator/interop/<script>/ - one subdirectory per
    script, so their artifacts never collide.
    """
    print("\n" + "=" * 60)
    print("Cross-language Interop Tests (Rust/Zig/C++)")
    print("=" * 60)

    missing = missing_headers()
    if missing:
        print(
            "[FAIL] missing required headers: "
            + ", ".join(str(path) for path in missing),
            file=sys.stderr,
        )
        return 1

    amc_bin_dir = build_dir / "bin"
    source_root = str(PROJECT_ROOT)
    interop_dir = PROJECT_ROOT / "AMC" / "tests" / "interop"

    scripts = [
        ("interop_test.py", "C++ -> ABIX -> Rust/Zig"),
        ("expose_test.py", "C++ -> ABIX -> Rust (auto capability generation)"),
        ("cmdly_test.py", "cmdly C++ -> ABIX -> Rust"),
    ]

    # Redirect each interop script's working directory to its own subdirectory
    # of build/<build_type>/generator/interop/, so the three suites (which all
    # use names like `rust_consumer.rs` and `gen/amc_generated.rs`) cannot
    # clobber each other's artifacts.
    interop_root = build_dir / "generator" / "interop"

    exit_code = 0
    for script_name, description in scripts:
        script_path = interop_dir / script_name
        if not script_path.exists():
            print(f"\n  [SKIP] {script_name} not found")
            continue

        print(f"\n--- {description} ---")
        cmd = [
            sys.executable,
            str(script_path),
            str(amc_bin_dir),
            source_root,
        ]
        if keep:
            cmd.append("--keep-artifacts")

        # Per-script working directory + artifact retention.
        script_work = interop_root / script_name[:-3]
        script_work.mkdir(parents=True, exist_ok=True)
        env = dict(os.environ)
        env["AMC_INTEROP_WORK_DIR"] = str(script_work)
        if keep:
            env["KEEP_ARTIFACTS"] = "1"
        if cmdly_root:
            env["CMDLY_ROOT"] = cmdly_root

        print(f"  \033[90m{' '.join(cmd)}\033[0m")
        result = subprocess.run(cmd, env=env)
        if result.returncode != 0:
            print(f"\033[31m[FAIL] {script_name} exited with code {result.returncode}\033[0m")
            exit_code = result.returncode
        else:
            print(f"\033[32m[PASS] {script_name}\033[0m")

    return exit_code


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Unified test runner for the ABIX project",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
Test suites:
  catch2      Catch2 C++ test suite (test_all)
  elf-pe      ELF/PE metadata section verification
  bootstrap   AMC core metadata bootstrap verification

Examples:
  python tools/run_test.py                        # all tests
  python tools/run_test.py --catch2              # Catch2 only
  python tools/run_test.py --elf-pe              # ELF/PE metadata only
  python tools/run_test.py --bootstrap           # AMC bootstrap only
  python tools/run_test.py --interop             # Cross-language interop only
  python tools/run_test.py --cmdly-root ~/src/cmdly  # enable the cmdly comparison
  python tools/run_test.py --build-dir build/Debug
  python tools/run_test.py --keep                # keep temp files
""",
    )
    parser.add_argument(
        "--catch2",
        action="store_true",
        help="Run Catch2 C++ test suite only",
    )
    parser.add_argument(
        "--elf-pe",
        action="store_true",
        help="Run ELF/PE metadata section verification only",
    )
    parser.add_argument(
        "--bootstrap",
        action="store_true",
        help="Run AMC core metadata bootstrap only",
    )
    parser.add_argument(
        "--interop",
        action="store_true",
        help="Run cross-language interop tests (Rust/Zig/C++ bindings)",
    )
    parser.add_argument(
        "--build-dir",
        default="build/Release",
        help="Build directory (default: build/Release)",
    )
    parser.add_argument(
        "--keep",
        action="store_true",
        help="Keep temporary test files",
    )
    parser.add_argument(
        "--cmdly-root",
        default="",
        metavar="PATH",
        help="Path to the cmdly checkout used by the interop cmdly comparison "
             "(sets CMDLY_ROOT; default: the scripts' own ~/Code/cmdly)",
    )
    args = parser.parse_args()

    # If no specific test is selected, run all
    run_all = not (args.catch2 or args.elf_pe or args.bootstrap or args.interop)

    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = PROJECT_ROOT / build_dir

    exit_code = 0

    if run_all or args.catch2:
        rc = run_catch2_tests(build_dir)
        if rc != 0:
            exit_code = rc

    if run_all or args.elf_pe:
        rc = run_elf_pe_tests(build_dir, keep=args.keep)
        if rc != 0:
            exit_code = rc

    if run_all or args.bootstrap:
        rc = run_bootstrap_test(build_dir, keep=args.keep)
        if rc != 0:
            exit_code = rc

    if run_all or args.interop:
        cmdly_root = str(Path(args.cmdly_root).expanduser()) if args.cmdly_root else None
        rc = run_interop_tests(build_dir, keep=True, cmdly_root=cmdly_root)
        if rc != 0:
            exit_code = rc

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
