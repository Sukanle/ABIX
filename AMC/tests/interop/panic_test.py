#!/usr/bin/env python3
"""Rust panic / extern "C" boundary test.

Usage:
    panic_test.py [amc_bin_dir]

Compiles a test-only Rust cdylib and checks two paths:

1. a provider that catches its own panic and returns an explicit status;
2. a provider whose panic escapes `extern "C"`, which must abort in a child
   process instead of unwinding into the caller.

The test is intentionally independent of the ABIX runtime. It validates the
provider-side contract that the Rust projection relies on.
"""

import ctypes
import os
import shutil
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


def exe_name(base: str) -> str:
    return base + ".exe" if sys.platform == "win32" else base


def lib_name(base: str) -> str:
    if sys.platform == "win32":
        return base + ".dll"
    if sys.platform == "darwin":
        return "lib" + base + ".dylib"
    return "lib" + base + ".so"


def child_call(path: str) -> int:
    """Call the uncaught-panic entry in a fresh process."""
    lib = ctypes.CDLL(path)
    fn = lib.abix_panic_now
    fn.argtypes = []
    fn.restype = None
    fn()
    # If this line is reached, the panic did not abort the process. The parent
    # treats this marker as a failure.
    print("RETURNED")
    return 0


def main() -> int:
    if len(sys.argv) >= 3 and sys.argv[1] == "--child":
        return child_call(sys.argv[2])

    rustc = shutil.which("rustc")
    if not rustc:
        print("  [SKIP] rustc not found: Rust panic boundary test")
        return 0

    if sys.platform not in ("win32", "darwin") and not sys.platform.startswith("linux"):
        print(f"  [SKIP] unsupported platform for the panic boundary test: {sys.platform}")
        return 0

    work = tempfile.mkdtemp(prefix="amc-rust-panic.")
    source = os.path.join(SCRIPT_DIR, "panic_provider.rs")
    library = os.path.join(work, lib_name("panic_provider"))
    passed = 0
    failed = 0

    def check(ok: bool, message: str, detail: str = "") -> None:
        nonlocal passed, failed
        if ok:
            passed += 1
            print(f"  [PASS] {message}")
        else:
            failed += 1
            print(f"  [FAIL] {message}")
            if detail:
                print(detail)

    try:
        result = subprocess.run(
            [rustc, "--edition", "2021", "--crate-type", "cdylib", "-O", source, "-o", library],
            capture_output=True,
            text=True,
        )
        check(result.returncode == 0 and os.path.isfile(library),
              "test-only Rust panic provider compiled",
              (result.stderr + result.stdout).strip()[:2000])
        if not os.path.isfile(library):
            print(f"\npanic boundary: {passed} passed, {failed} failed")
            return 1

        lib = ctypes.CDLL(library)
        catch = lib.abix_catch_panic
        catch.argtypes = []
        catch.restype = ctypes.c_int
        status = catch()
        check(status == 77, "caught panic becomes an explicit status code",
              f"expected 77, got {status}")

        try:
            child = subprocess.run(
                [sys.executable, os.path.abspath(__file__), "--child", library],
                capture_output=True,
                text=True,
                timeout=30,
            )
            check(child.returncode != 0,
                  "uncaught panic aborts instead of unwinding into the caller",
                  f"child exited with {child.returncode}")
            check("RETURNED" not in child.stdout,
                  "uncaught panic never returns normally",
                  (child.stderr + child.stdout).strip()[:2000])
        except subprocess.TimeoutExpired:
            check(False, "uncaught panic test completed",
                  "child process timed out; an uncaught panic may have unwound or hung")

        print(f"\npanic boundary: {passed} passed, {failed} failed")
        return 0 if failed == 0 else 1
    finally:
        if os.environ.get("KEEP_ARTIFACTS", "") == "1":
            print(f"  [KEEP] retained artifacts: {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
