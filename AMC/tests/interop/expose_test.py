#!/usr/bin/env python3
"""Automatic capability generation test.

Usage:
    expose_test.py <amc_bin_dir> [source_root]

Proves the P0 automation loop for a plain C++ class, with **no hand-written
adapter**: AMC reads the class AST/layout, generates the C thunks
(`abix_thunks.cpp`), records the capabilities in `.abix`, projects them to Rust,
and a Rust consumer creates the object, calls its methods and lets the creating
module destroy it.

Nothing here hand-writes a C header, a method adapter, an interface table or a
consumer binding: they are all generated. `Counter::value_` is private, so the
consumer must not reach it — the class projects as an opaque handle.
"""

import os
import shutil
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: expose_test.py <amc_bin_dir> [source_root]", file=sys.stderr)
        return 2
    bin_dir = os.path.abspath(sys.argv[1])
    source_root = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else os.path.abspath(
        os.path.join(SCRIPT_DIR, "..", "..", ".."))
    amc = os.path.join(bin_dir, "amc")
    cxx = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
    rustc = shutil.which("rustc")

    missing = []
    if not os.path.isfile(amc):
        missing.append("amc")
    if not cxx:
        missing.append("a C++ compiler")
    if not rustc:
        missing.append("rustc")
    if missing:
        print("  [SKIP] automatic capability generation needs: " + ", ".join(missing))
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

    work = tempfile.mkdtemp(prefix="amc-expose.")
    fixture = os.path.join(SCRIPT_DIR, "expose")
    for name in ("counter.hpp", "counter.abic.toml", "consumer.rs", "consumer.cpp"):
        shutil.copy(os.path.join(fixture, name), os.path.join(work, name))

    # 1. Describe the class and generate its thunks.
    r = run([amc, "build", "-c", os.path.join(work, "counter.abic.toml"), "-B", os.path.join(work, "out")])
    if not check(r.returncode == 0, "amc build generated the metadata and thunks", (r.stdout + r.stderr)[-400:]):
        return 1
    thunks = os.path.join(work, "abix_thunks.cpp")
    check(os.path.isfile(thunks), "abix_thunks.cpp was generated")
    abix = os.path.join(work, "out", "counter_expose.abix")

    # 2. Compile the generated thunks into the producing module.
    suffix = ".dylib" if sys.platform == "darwin" else ".so"
    module = os.path.join(work, "libcounter" + suffix)
    r = run([cxx, "-std=c++17", "-fPIC", "-shared", "-O2", thunks, "-I" + source_root, "-o", module])
    if not check(r.returncode == 0, "the generated thunks compile", r.stderr[-400:]):
        return 1
    check("abix_module_query_v1" in subprocess.run(["nm", "-D", module], capture_output=True, text=True).stdout,
          "the module exports the fixed discovery entry")

    # 3. Project the capabilities to Rust.
    gen = os.path.join(work, "gen")
    os.makedirs(gen, exist_ok=True)
    r = run([amc, "generate", abix, "-l", "rust", "-o", gen])
    if not check(r.returncode == 0, "amc generate -l rust", (r.stdout + r.stderr)[-400:]):
        return 1
    generated = open(os.path.join(gen, "amc_generated.rs"), encoding="utf-8").read()
    check("pub struct Counter([u8; 4])" in generated, "the class projects as an opaque handle")
    check("pub struct OwnedCounter" in generated, "an owner wrapper was generated")

    # 4. Compile and run the Rust consumer (create / get / add / destroy).
    consumer = os.path.join(work, "consumer")
    r = run([
        rustc, "--edition", "2021", os.path.join(work, "consumer.rs"), "-o", consumer,
        "-L", "native=" + work, "-l", "dylib=counter",
        "-C", "link-arg=-Wl,-rpath," + work,
    ])
    if not check(r.returncode == 0, "rustc compiled the consumer", r.stderr[-400:]):
        return 1
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = work + os.pathsep + env.get("LD_LIBRARY_PATH", "")
    env["DYLD_LIBRARY_PATH"] = work + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
    r = run([consumer], env=env)
    check(r.returncode == 0 and "expose counter" in r.stdout,
          "Rust created, called and destroyed the C++ object", (r.stdout + r.stderr)[-400:])

    # 5/8/9: discovery through the fixed entry, binding once, and a module lease
    # that keeps the module loaded while the object is alive.
    consumer_cpp = os.path.join(work, "consumer_cpp")
    r = run([cxx, "-std=c++17", os.path.join(work, "consumer.cpp"), "-I" + source_root, "-o", consumer_cpp])
    if not check(r.returncode == 0, "the registry consumer compiles", r.stderr[-400:]):
        return 1
    r = run([consumer_cpp, module], env=env)
    check(r.returncode == 0 and "expose registry: OK" in r.stdout,
          "registry bus discovered, bound and leased the exposed capabilities", (r.stdout + r.stderr)[-400:])

    shutil.rmtree(work, ignore_errors=True)
    print("=== automatic capability generation: %d passed, %d failed ===" % (passed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
