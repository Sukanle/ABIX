#!/usr/bin/env python3
"""Build and run the standalone cross-language example.

    python3 example/cross_language/run_example.py

The example has one native C++ library, one `.abic.toml` provider description,
and C++, Rust and Zig consumers. AMC derives the ABI from the public header once;
all three consumers use the same generated artifact and link the same library.

Requirements: a C++17 compiler, `amc`, `rustc` and `zig`.
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BUILD = HERE / "build"


def exe_name(base: str) -> str:
    return base + ".exe" if sys.platform == "win32" else base


def find_tool(explicit, env_name, names, hint):
    if explicit:
        return explicit
    env = os.environ.get(env_name)
    if env:
        return env
    for name in names:
        found = shutil.which(name)
        if found:
            return found
    raise SystemExit(f"missing {hint}; pass the path explicitly or add it to PATH")


def run(command, env=None, label=None):
    if label:
        print(f"[example] {label}", flush=True)
    result = subprocess.run(command, capture_output=True, text=True, env=env)
    if result.returncode != 0:
        print("command failed:", " ".join(str(part) for part in command), file=sys.stderr)
        if result.stdout:
            print(result.stdout, file=sys.stderr)
        if result.stderr:
            print(result.stderr, file=sys.stderr)
        raise SystemExit(result.returncode)
    if result.stdout.strip():
        print(result.stdout.rstrip(), flush=True)
    if result.stderr.strip():
        print(result.stderr.rstrip(), flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--amc", help="path to amc (default: AMC, build/Release/bin/amc, or PATH)")
    parser.add_argument("--cxx", help="C++ compiler")
    parser.add_argument("--rustc", help="Rust compiler")
    parser.add_argument("--zig", help="Zig compiler")
    parser.add_argument("--skip-missing", action="store_true",
                        help="return success when an optional consumer toolchain is absent")
    args = parser.parse_args()

    try:
        amc = find_tool(args.amc or os.environ.get("AMC"), "AMC",
                        [str(ROOT / "build" / "Release" / "bin" / exe_name("amc")), exe_name("amc")],
                        "amc")
        cxx = find_tool(args.cxx, "CXX", ["clang++", "c++"], "C++ compiler")
        rustc = find_tool(args.rustc, "RUSTC", ["rustc"], "rustc")
        zig = find_tool(args.zig, "ZIG", ["zig"], "zig")
    except SystemExit as error:
        if args.skip_missing:
            print(f"[SKIP] {error}")
            return 0
        raise

    if BUILD.exists():
        shutil.rmtree(BUILD)
    gen = BUILD / "gen"
    gen.mkdir(parents=True)

    library_name = "shapes.dll" if sys.platform == "win32" else (
        "libshapes.dylib" if sys.platform == "darwin" else "libshapes.so")
    library = BUILD / library_name
    native = [cxx, "-std=c++17", "-O2", "-DSHAPES_BUILD", "-shared"]
    if sys.platform == "win32":
        import_library = BUILD / "libshapes.dll.a"
        native += [str(HERE / "native" / "shapes.cpp"), "-o", str(library),
                   f"-Wl,--out-implib,{import_library}"]
    else:
        native += ["-fPIC", str(HERE / "native" / "shapes.cpp"), "-o", str(library)]
    run(native, label="building native/shapes.cpp -> " + library.name)

    config = HERE / "shapes.abic.toml"
    run([amc, "build", "-c", str(config), "-B", str(BUILD)], label="amc build -> shapes.abix")
    artifact = BUILD / "shapes.abix"
    if not artifact.is_file():
        raise SystemExit(f"amc build did not produce {artifact}")

    run([amc, "generate", str(artifact), "-l", "cpp", "-o", str(gen / "amc_generated.hpp")],
        label="amc generate -l cpp")
    run([amc, "generate", str(artifact), "-l", "rust", "-o", str(gen)],
        label="amc generate -l rust")
    run([amc, "generate", str(artifact), "-l", "zig", "-o", str(gen)],
        label="amc generate -l zig")

    # Rust and Zig use paths relative to their own file; compile the copies in
    # the build directory so `gen/amc_generated.*` resolves consistently.
    shutil.copyfile(HERE / "rust_consumer.rs", BUILD / "rust_consumer.rs")
    shutil.copyfile(HERE / "zig_consumer.zig", BUILD / "zig_consumer.zig")

    run([cxx, "-std=c++17", "-O2", str(HERE / "cpp_consumer.cpp"),
         f"-I{HERE}", f"-I{BUILD}", f"-I{ROOT}", f"-L{BUILD}", "-lshapes",
         "-o", str(BUILD / exe_name("cpp_consumer"))],
        label="compiling cpp_consumer.cpp")
    run([rustc, "--edition", "2021", "-O", str(BUILD / "rust_consumer.rs"),
         "--cfg", "abix_link", "-L", f"native={BUILD}",
         "-o", str(BUILD / exe_name("rust_consumer"))],
        label="compiling rust_consumer.rs")
    run([zig, "build-exe", str(BUILD / "zig_consumer.zig"), "-lc",
         "-L", str(BUILD), "-lshapes",
         "-femit-bin=" + str(BUILD / exe_name("zig_consumer"))],
        label="compiling zig_consumer.zig")

    env = dict(os.environ)
    if sys.platform == "win32":
        env["PATH"] = str(BUILD) + os.pathsep + env.get("PATH", "")
    elif sys.platform == "darwin":
        env["DYLD_LIBRARY_PATH"] = str(BUILD) + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
    else:
        env["LD_LIBRARY_PATH"] = str(BUILD) + os.pathsep + env.get("LD_LIBRARY_PATH", "")

    run([str(BUILD / exe_name("cpp_consumer"))], env=env, label="running C++ consumer")
    run([str(BUILD / exe_name("rust_consumer"))], env=env, label="running Rust consumer")
    run([str(BUILD / exe_name("zig_consumer"))], env=env, label="running Zig consumer")

    print(f"[example] done; generated files are under {BUILD}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
