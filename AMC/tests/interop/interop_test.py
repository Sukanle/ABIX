#!/usr/bin/env python3
"""M1 C++ -> ABIX -> Rust interop test.

Usage:
    interop_test.py <amc_bin_dir> [source_root]

This is the end-to-end proof for milestone M1: a real native shared library is
described by AMC into `interop_demo.abix`, projected to Rust with
`amc generate -l rust`, and then exercised by a compiled-and-run Rust consumer
that links the library.

The steps are ordered so that a failure points at one layer:

    1. build the native demo library (exported C ABI);
    2. `amc build` derives the ABI from the public header into `.abix`;
    3. `amc generate -l rust` projects that artifact to Rust;
    4. `rustc` compiles the consumer (its compile-time layout assertions run);
    5. the consumer runs and its run-time assertions must hold.
"""

import os
import shutil
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: interop_test.py <amc_bin_dir> [source_root]", file=sys.stderr)
        return 2

    bin_dir = os.path.abspath(sys.argv[1])
    amc = os.path.join(bin_dir, "amc")

    cxx = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
    rustc = shutil.which("rustc")
    if not os.path.isfile(amc) or not cxx or not rustc:
        print(f"  [SKIP] need amc, a C++ compiler and rustc (amc={os.path.isfile(amc)}, cxx={cxx}, rustc={rustc})")
        return 0

    if sys.platform == "darwin":
        lib_name, load_env = "libinterop_demo.dylib", "DYLD_LIBRARY_PATH"
    elif sys.platform.startswith("linux"):
        lib_name, load_env = "libinterop_demo.so", "LD_LIBRARY_PATH"
    else:
        print(f"  [SKIP] unsupported platform for the interop demo: {sys.platform}")
        return 0

    work = tempfile.mkdtemp(prefix="amc-interop.")
    passed = 0
    failed = 0

    def run(args, **kwargs):
        return subprocess.run(args, capture_output=True, text=True, **kwargs)

    def check(ok, message, detail=""):
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
        # 1. Native shared library with the exported C ABI.
        lib = os.path.join(work, lib_name)
        result = run(
            [cxx, "-std=c++17", "-shared", "-fPIC", "-O2",
             os.path.join(SCRIPT_DIR, "demo_lib.cpp"), "-o", lib]
        )
        check(result.returncode == 0, "native demo library built",
              result.stderr.strip()[:2000])

        # 2. AMC derives the ABI from the public header.
        abix_dir = os.path.join(work, "abix")
        result = run([amc, "build", "-c", os.path.join(SCRIPT_DIR, "interop_demo.abic.toml"),
                      "-B", abix_dir])
        artifact = os.path.join(abix_dir, "interop_demo.abix")
        check(result.returncode == 0 and os.path.isfile(artifact),
              "amc build produced interop_demo.abix",
              (result.stderr.strip() + result.stdout.strip())[:2000])

        # 3. Project the artifact to Rust.
        gen_dir = os.path.join(work, "gen")
        result = run([amc, "generate", artifact, "-l", "rust", "-o", gen_dir]) if os.path.isfile(artifact) else None
        generated = os.path.join(gen_dir, "amc_generated.rs")
        check(result is not None and result.returncode == 0 and os.path.isfile(generated),
              "amc generate -l rust produced amc_generated.rs",
              (result.stderr.strip() if result else "skipped (no artifact)")[:2000])

        # 4. Compile the Rust consumer against the generated projection. The
        #    `abix_link` cfg turns the projection's `#[link(name = ...)]` on, so
        #    the symbols are resolved against the native library at link time
        #    (no hand-written `-l` on the command line).
        consumer_src = os.path.join(work, "consumer.rs")
        shutil.copyfile(os.path.join(SCRIPT_DIR, "consumer.rs"), consumer_src)
        consumer = os.path.join(work, "interop_consumer")
        result = run([rustc, "--edition", "2021", "-O", consumer_src,
                      "--cfg", "abix_link", "-L", work, "-o", consumer])
        check(result.returncode == 0, "Rust consumer compiled against the projection",
              result.stderr.strip()[:2000])

        # 5. Run the consumer against the native library.
        if os.path.isfile(consumer):
            env = dict(os.environ)
            env[load_env] = work + os.pathsep + env.get(load_env, "")
            result = run([consumer], env=env)
            check(result.returncode == 0 and "interop ok" in result.stdout,
                  "Rust consumer ran the native library",
                  (result.stderr.strip() + result.stdout.strip())[:2000])
        else:
            check(False, "Rust consumer ran the native library", "consumer was not built")

        # 6-8. The same artifact drives a second language backend (Zig),
        #      validating that `.abix` is a reusable IR rather than a
        #      single-language binding.
        zig = shutil.which("zig")
        if not zig:
            print("  [SKIP] zig not found: skipping the Zig projection")
        else:
            zig_gen = os.path.join(work, "gen_zig")
            result = run([amc, "generate", artifact, "-l", "zig", "-o", zig_gen]) if os.path.isfile(artifact) else None
            zig_projection = os.path.join(zig_gen, "amc_generated.zig")
            check(result is not None and result.returncode == 0 and os.path.isfile(zig_projection),
                  "amc generate -l zig produced amc_generated.zig",
                  (result.stderr.strip() if result else "skipped (no artifact)")[:2000])

            zig_src = os.path.join(work, "zig_consumer.zig")
            shutil.copyfile(os.path.join(SCRIPT_DIR, "zig_consumer.zig"), zig_src)
            zig_exe = os.path.join(work, "zig_consumer")
            result = run([zig, "build-exe", zig_src, "-lc", "-L", work, "-linterop_demo",
                          "-femit-bin=" + zig_exe])
            check(result.returncode == 0, "Zig consumer compiled against the projection",
                  result.stderr.strip()[:2000])

            if os.path.isfile(zig_exe):
                env = dict(os.environ)
                env[load_env] = work + os.pathsep + env.get(load_env, "")
                result = run([zig_exe], env=env)
                check(result.returncode == 0 and "zig interop ok" in (result.stdout + result.stderr),
                      "Zig consumer ran the native library",
                      (result.stderr.strip() + result.stdout.strip())[:2000])
            else:
                check(False, "Zig consumer ran the native library", "zig consumer was not built")

        print(f"\ninterop: {passed} passed, {failed} failed")
        return 0 if failed == 0 else 1
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
