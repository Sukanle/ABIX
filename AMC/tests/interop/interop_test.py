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


def exe_name(base: str) -> str:
    return base + ".exe" if sys.platform == "win32" else base


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: interop_test.py <amc_bin_dir> [source_root]", file=sys.stderr)
        return 2

    bin_dir = os.path.abspath(sys.argv[1])
    amc = os.path.join(bin_dir, exe_name("amc"))

    cxx = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
    rustc = shutil.which("rustc")
    if not os.path.isfile(amc) or not cxx or not rustc:
        print(f"  [SKIP] need amc, a C++ compiler and rustc (amc={os.path.isfile(amc)}, cxx={cxx}, rustc={rustc})")
        return 0

    if sys.platform == "darwin":
        lib_name, load_env = "libinterop_demo.dylib", "DYLD_LIBRARY_PATH"
    elif sys.platform.startswith("linux"):
        lib_name, load_env = "libinterop_demo.so", "LD_LIBRARY_PATH"
    elif sys.platform == "win32":
        # Windows resolves the DLL from the executable's directory, so no
        # loader-path variable is needed at run time.
        lib_name, load_env = "interop_demo.dll", None
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
        demo_lib = os.path.join(SCRIPT_DIR, "demo_lib.cpp")
        if sys.platform == "win32":
            # Emit the import library rustc/zig link against alongside the DLL.
            implib = os.path.join(work, "libinterop_demo.dll.a")
            result = run([cxx, "-std=c++17", "-shared", "-O2", demo_lib, "-o", lib,
                          f"-Wl,--out-implib,{implib}"])
        else:
            result = run([cxx, "-std=c++17", "-shared", "-fPIC", "-O2", demo_lib, "-o", lib])
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

        # 3b. The same artifact projects to a C++ header. The interface contract
        #     must surface its identity, version, slot enum and adapter typedefs,
        #     so a consumer can declare `IVectorMath` without a private vtable.
        cpp_header = os.path.join(work, "amc_generated.hpp")
        result = run([amc, "generate", artifact, "-l", "cpp", "-o", cpp_header]) if os.path.isfile(artifact) else None
        if result is not None and result.returncode == 0 and os.path.isfile(cpp_header):
            header = open(cpp_header, encoding="utf-8").read()
            check("kIVectorMathInterface" in header and "kIVectorMathVersion" in header,
                  "amc generate -l cpp emitted the interface identity")
            check("enum class IVectorMathSlot" in header and "demo_add = 0" in header and "demo_scale = 1" in header,
                  "amc generate -l cpp emitted the interface slot enum")
            check("IVectorMath_demo_scale_fn" in header and "(*)(const struct DemoVec2*, double)" in header,
                  "amc generate -l cpp emitted the interface method typedefs")
        else:
            check(False, "amc generate -l cpp produced the interface projection",
                  (result.stderr.strip() if result else "skipped (no artifact)")[:2000])

        # 4. Compile the Rust consumer against the generated projection. The
        #    `abix_link` cfg turns the projection's `#[link(name = ...)]` on, so
        #    the symbols are resolved against the native library at link time
        #    (no hand-written `-l` on the command line).
        consumer_src = os.path.join(work, "consumer.rs")
        shutil.copyfile(os.path.join(SCRIPT_DIR, "consumer.rs"), consumer_src)
        consumer = os.path.join(work, exe_name("interop_consumer"))
        result = run([rustc, "--edition", "2021", "-O", consumer_src,
                      "--cfg", "abix_link", "-L", work, "-o", consumer])
        check(result.returncode == 0, "Rust consumer compiled against the projection",
              result.stderr.strip()[:2000])

        # 5. Run the consumer against the native library.
        if os.path.isfile(consumer):
            env = dict(os.environ)
            if load_env:
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
            zig_exe = os.path.join(work, exe_name("zig_consumer"))
            result = run([zig, "build-exe", zig_src, "-lc", "-L", work, "-linterop_demo",
                          "-femit-bin=" + zig_exe])
            check(result.returncode == 0, "Zig consumer compiled against the projection",
                  result.stderr.strip()[:2000])

            if os.path.isfile(zig_exe):
                env = dict(os.environ)
                if load_env:
                    env[load_env] = work + os.pathsep + env.get(load_env, "")
                result = run([zig_exe], env=env)
                check(result.returncode == 0 and "zig interop ok" in (result.stdout + result.stderr),
                      "Zig consumer ran the native library",
                      (result.stderr.strip() + result.stdout.strip())[:2000])
            else:
                check(False, "Zig consumer ran the native library", "zig consumer was not built")

        # 9-10. Static target-ABI verification (the CI entry): the artifact's
        #       recorded target is checked against an expected target.
        if os.path.isfile(artifact):
            expect = "arch=1,os=1,abi=1,pointer_bits=64,endianness=1,calling_convention=1"
            result = run([amc, "target", artifact, "--expect", expect])
            check(result.returncode == 0 and "platform: compatible" in result.stdout,
                  "amc target verifies the artifact target",
                  (result.stderr + result.stdout)[:2000])
            result = run([amc, "target", artifact, "--expect", "arch=2"])
            check(result.returncode == 1 and "platform: incompatible" in result.stdout,
                  "amc target rejects a mismatched target",
                  (result.stderr + result.stdout)[:2000])
        else:
            check(False, "amc target verifies the artifact target", "no artifact")

        print(f"\ninterop: {passed} passed, {failed} failed")
        return 0 if failed == 0 else 1
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
