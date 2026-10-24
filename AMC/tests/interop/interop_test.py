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

# `--keep-artifacts`, or KEEP_ARTIFACTS=1 in the environment (set by the CMake
# option SKL_ABIX_KEEP_TEST_ARTIFACTS), retains the temporary working directory.
if "--keep-artifacts" in sys.argv:
    os.environ["KEEP_ARTIFACTS"] = "1"
KEEP_ARTIFACTS = os.environ.get("KEEP_ARTIFACTS", "") == "1"


def exe_name(base: str) -> str:
    return base + ".exe" if sys.platform == "win32" else base


def toml_path(path: str) -> str:
    """Return a path safe to embed in a TOML string literal."""
    return path.replace("\\", "/")


def include_roots(source_root: str) -> tuple[str, ...]:
    """Public include roots of the SKL-layout source tree."""
    return (
        os.path.join(source_root, "include"),
        os.path.join(source_root, "MICS", "include"),
        os.path.join(source_root, "MICS", "HLMD"),
    )


def include_flags(source_root: str) -> list[str]:
    return ["-I" + root for root in include_roots(source_root)]


def toml_include_flags(source_root: str) -> str:
    return ", ".join('"-I%s"' % toml_path(root) for root in include_roots(source_root))


def main() -> int:
    argv = [arg for arg in sys.argv[1:] if arg != "--keep-artifacts"]
    if not argv:
        print("usage: interop_test.py <amc_bin_dir> [source_root]", file=sys.stderr)
        return 2

    bin_dir = os.path.abspath(argv[0])
    source_root = os.path.abspath(argv[1]) if len(argv) > 1 else os.path.abspath(
        os.path.join(SCRIPT_DIR, "..", "..", ".."))
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

    work = os.environ.get("AMC_INTEROP_WORK_DIR") or tempfile.mkdtemp(prefix="amc-interop.")
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
            result = run([cxx, "-std=c++17", "-shared", "-O2", demo_lib,
                          *include_flags(source_root), "-o", lib,
                          f"-Wl,--out-implib,{implib}"])
        else:
            result = run([cxx, "-std=c++17", "-shared", "-fPIC", "-O2", demo_lib,
                          *include_flags(source_root), "-o", lib])
        check(result.returncode == 0, "native demo library built",
              result.stderr.strip()[:2000])

        # 2. AMC derives the ABI from the public header. The import flags carry
        #    the SKL include roots so the fixture resolves ABIX-STD headers from
        #    the source tree instead of relying on the caller's environment.
        shutil.copyfile(
            os.path.join(SCRIPT_DIR, "demo_api.hpp"),
            os.path.join(work, "demo_api.hpp"),
        )
        config = os.path.join(work, "interop_demo.abic.toml")
        config_text = open(
            os.path.join(SCRIPT_DIR, "interop_demo.abic.toml"), encoding="utf-8"
        ).read().replace(
            'flags = ["-std=c++17"]',
            'flags = ["-std=c++17", ' + toml_include_flags(source_root) + "]",
        )
        open(config, "w", encoding="utf-8").write(config_text)
        abix_dir = os.path.join(work, "abix")
        result = run([amc, "build", "-c", config, "-B", abix_dir])
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

        if os.path.isfile(generated):
            generated_text = open(generated, encoding="utf-8").read()
            check("demo_div_result" in generated_text and "pub struct DemoDivError(pub" in generated_text,
                  "Rust projection emits the error-boundary Result and an FFI-safe error newtype")
            check("demo_div_unknown_result" in generated_text
                  and "pub struct DemoDivUnknownError(pub" in generated_text,
                  "Rust projection keeps an FFI-safe newtype for an unknown-status error boundary")
            check("demo_add_result" not in generated_text,
                  "Rust projection does not invent a Result wrapper for a function without an error contract")

        # 3b. The same artifact projects to a C++ header. The interface contract
        #     must surface its identity, version, typed slot table and adapter
        #     typedefs, so a consumer can declare `IVectorMath` without a private
        #     vtable.
        cpp_header = os.path.join(work, "amc_generated.hpp")
        result = run([amc, "generate", artifact, "-l", "cpp", "-o", cpp_header]) if os.path.isfile(artifact) else None
        if result is not None and result.returncode == 0 and os.path.isfile(cpp_header):
            header = open(cpp_header, encoding="utf-8").read()
            check("kIVectorMathInterface" in header and "kIVectorMathVersion" in header,
                  "amc generate -l cpp emitted the interface identity")
            check("struct alignas(8) Slots" in header and "uint32_t demo_add;" in header
                  and "uint32_t demo_scale;" in header,
                  "amc generate -l cpp emitted the typed slot table")
            check("alignas(8) static constexpr Slots slots" in header
                  and "static constexpr ::skl::mics::util::array signatures" in header,
                  "amc generate -l cpp emitted the signature array and zero-based slot table")
            check("IVectorMath_demo_scale_fn" in header and "(*)(const struct DemoVec2*, double)" in header,
                  "amc generate -l cpp emitted the interface method typedefs")
        else:
            check(False, "amc generate -l cpp produced the interface projection",
                  (result.stderr.strip() if result else "skipped (no artifact)")[:2000])

        # 4. Compile the Rust consumer against the generated projection. The
        #    `abix_link` cfg turns the projection's `#[link(name = ...)]` on, so
        #    the symbols are resolved against the native library at link time
        #    (no hand-written `-l` on the command line).
        consumer_src = os.path.join(work, "rust_consumer.rs")
        shutil.copyfile(os.path.join(SCRIPT_DIR, "rust_consumer.rs"), consumer_src)
        consumer = os.path.join(work, exe_name("rust_consumer"))
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

        # 5b. The same artifact also projects to C++ and compiles against the
        #     generated header. This makes the interop suite exercise all three
        #     consumers against the same native library.
        cpp_consumer_src = os.path.join(SCRIPT_DIR, "cpp_consumer.cpp")
        cpp_consumer = os.path.join(work, exe_name("cpp_consumer"))
        result = run([cxx, "-std=c++17", "-O2", cpp_consumer_src,
                      "-I", work, *include_flags(source_root),
                      "-L", work, "-linterop_demo", "-o", cpp_consumer])
        check(result.returncode == 0, "C++ consumer compiled against the projection",
              result.stderr.strip()[:2000])

        if os.path.isfile(cpp_consumer):
            env = dict(os.environ)
            if load_env:
                env[load_env] = work + os.pathsep + env.get(load_env, "")
            result = run([cpp_consumer], env=env)
            check(result.returncode == 0 and "cpp interop ok" in result.stdout,
                  "C++ consumer ran the native library",
                  (result.stderr.strip() + result.stdout.strip())[:2000])
        else:
            check(False, "C++ consumer ran the native library", "consumer was not built")

        # 6-8. The same artifact drives a second language backend (Zig),
        #      validating that `.abix` is a reusable IR rather than a
        #      single-language binding.
        zig = shutil.which("zig")
        if not zig:
            print("  [SKIP] zig not found: skipping the Zig projection")
        else:
            zig_gen = os.path.join(work, "gen")
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
        if KEEP_ARTIFACTS:
            print(f"  [KEEP] retained artifacts: {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
