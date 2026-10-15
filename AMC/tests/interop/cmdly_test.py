#!/usr/bin/env python3
"""Cmdly interop test: an ABIX C facade over the cmdly C++ library.

Usage:
    cmdly_test.py <amc_bin_dir> [source_root]

This is a real-library cross-language demo. Cmdly is a C++17 line editor whose
public API is C++-only (classes, std::shared_ptr, templates, STL). The fixture
here adds a thin C ABI facade (`cmdly_c.h` / `cmdly_c.cpp`), describes it with
AMC, projects it to Rust, and runs a consumer that:

    1. drives cmdly through the C entry points,
    2. uses the owner ABI (RAII handle, released by the library's destroy entry),
    3. registers a cross-language key callback,
    4. reads a scripted line deterministically (no tty).

The test skips cleanly when cmdly, its built library, fmt or rustc are missing.
Point `CMDLY_ROOT` at the cmdly checkout to override the default `~/Code/cmdly`.
"""

import os
import shutil
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


def find_cmdly():
    root = os.environ.get("CMDLY_ROOT") or os.path.expanduser("~/Code/cmdly")
    include = os.path.join(root, "libcmdly", "include")
    for candidate in ("build/Release/libcmdly", "build/libcmdly", "build/lib", "lib"):
        directory = os.path.join(root, candidate)
        for name in ("libcmdly.so", "libcmdly.dylib"):
            if os.path.isfile(os.path.join(directory, name)):
                return root, include, directory
    return root, include, None


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: cmdly_test.py <amc_bin_dir> [source_root]", file=sys.stderr)
        return 2
    bin_dir = os.path.abspath(sys.argv[1])
    amc = os.path.join(bin_dir, "amc")
    cxx = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
    rustc = shutil.which("rustc")
    cmdly_root, cmdly_include, cmdly_lib = find_cmdly()

    missing = []
    if not os.path.isfile(amc):
        missing.append("amc")
    if not cxx:
        missing.append("a C++ compiler")
    if not rustc:
        missing.append("rustc")
    if not os.path.isdir(cmdly_include):
        missing.append("cmdly headers at " + cmdly_include)
    if not cmdly_lib:
        missing.append("a built libcmdly under " + cmdly_root)
    if missing:
        print("  [SKIP] cmdly interop needs: " + ", ".join(missing))
        return 0

    passed = 0
    failed = 0

    def check(ok, message, detail=""):
        nonlocal passed, failed
        if ok:
            passed += 1
            print("  [PASS] " + message)
        else:
            failed += 1
            print("  [FAIL] " + message + ((" :: " + detail) if detail else ""))
        return ok

    def run(args, env=None):
        return subprocess.run(args, capture_output=True, text=True, env=env)

    work = tempfile.mkdtemp(prefix="amc-cmdly.")
    fixture = os.path.join(SCRIPT_DIR, "cmdly")
    for name in ("cmdly_c.h", "cmdly_c.cpp", "cmdly.abic.toml", "consumer.rs"):
        shutil.copy(os.path.join(fixture, name), os.path.join(work, name))

    suffix = ".dylib" if sys.platform == "darwin" else ".so"
    facade = os.path.join(work, "libcmdly_c" + suffix)

    # 1. Build the C facade over cmdly.
    r = run([
        cxx, "-std=c++17", "-fPIC", "-shared", "-O2",
        os.path.join(work, "cmdly_c.cpp"),
        "-I" + cmdly_include,
        "-I" + os.path.join(cmdly_root, "libcmdly", "include"),
        "-L" + cmdly_lib, "-lcmdly",
        "-Wl,-rpath," + cmdly_lib,
        "-o", facade,
    ])
    if not check(r.returncode == 0, "compiled the cmdly C facade", r.stderr[-400:]):
        return 1

    # 2. Describe the facade with AMC.
    r = run([amc, "build", "-c", os.path.join(work, "cmdly.abic.toml"), "-B", os.path.join(work, "out")], env=None)
    if not check(r.returncode == 0, "amc build produced .abix", (r.stdout + r.stderr)[-400:]):
        return 1
    abix = os.path.join(work, "out", "cmdly_demo.abix")
    out_dir = os.path.join(work, "out")

    # 3. Validate and project to Rust.
    r = run([amc, "validate", abix])
    check(r.returncode == 0, "amc validate", r.stderr[-300:])
    gen = os.path.join(work, "gen")
    os.makedirs(gen, exist_ok=True)
    r = run([amc, "generate", abix, "-l", "rust", "-o", gen])
    if not check(r.returncode == 0, "amc generate -l rust", (r.stdout + r.stderr)[-400:]):
        return 1

    # 4. Compile the Rust consumer against the generated projection.
    consumer = os.path.join(work, "consumer")
    r = run([
        rustc, "--edition", "2021", os.path.join(work, "consumer.rs"), "-o", consumer,
        "-L", "native=" + work, "-l", "dylib=cmdly_c",
        "-C", "link-arg=-Wl,-rpath-link," + cmdly_lib,
        "-C", "link-arg=-Wl,-rpath," + work,
        "-C", "link-arg=-Wl,-rpath," + cmdly_lib,
    ])
    if not check(r.returncode == 0, "rustc compiled the consumer", r.stderr[-400:]):
        return 1

    # 5. Run it.
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = work + os.pathsep + cmdly_lib + os.pathsep + env.get("LD_LIBRARY_PATH", "")
    env["DYLD_LIBRARY_PATH"] = work + os.pathsep + cmdly_lib + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
    r = run([consumer], env=env)
    check(r.returncode == 0 and "cmdly ABIX interop" in r.stdout,
          "the Rust consumer drove cmdly through the ABIX facade", (r.stdout + r.stderr)[-400:])

    shutil.rmtree(work, ignore_errors=True)
    print("=== cmdly interop: %d passed, %d failed ===" % (passed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
