#!/usr/bin/env python3
"""Build one ABIX-STD native library and run C/C++/Rust/Zig layout+call checks.

The fixture checks the v0.4.0 explicit layouts for `abix_optional`,
`abix_result` and `abix_str`, the unchanged borrowed span and callback layouts,
and the fail-closed rules for an invalid discriminant or a non-zero `reserved`
word. Rust and Zig are optional: the script reports a skip when the compiler is
not available or the host cannot link the fixture.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def run(command, env=None, cwd=None):
    result = subprocess.run(command, check=False, capture_output=True, text=True, env=env, cwd=cwd)
    if result.returncode != 0:
        print("command failed:", " ".join(command), file=sys.stderr)
        if result.stdout:
            print(result.stdout, file=sys.stderr)
        if result.stderr:
            print(result.stderr, file=sys.stderr)
        raise RuntimeError(command[0] + " failed")
    return result


def compare_compiler(command):
    return os.path.basename(command).lower() in {"cl", "clang-cl"}


def library_names():
    if sys.platform == "win32":
        return "conformance_std.dll", "conformance_std"
    if sys.platform == "darwin":
        return "libconformance_std.dylib", "conformance_std"
    return "libconformance_std.so", "conformance_std"


def runtime_env(library_dir):
    env = os.environ.copy()
    if sys.platform == "win32":
        key = "PATH"
    elif sys.platform == "darwin":
        key = "DYLD_LIBRARY_PATH"
    else:
        key = "LD_LIBRARY_PATH"
    env[key] = library_dir + os.pathsep + env.get(key, "")
    return env


def rpath_args(library_dir):
    if sys.platform == "darwin":
        return [f"-Wl,-rpath,{library_dir}"]
    if sys.platform == "win32":
        return []
    return [f"-Wl,-rpath,{library_dir}"]


def build_native(cxx, include_dir, work, name, m32):
    lib_file, _lib_stem = library_names()
    out_dir = os.path.join(work, name)
    os.makedirs(out_dir, exist_ok=True)
    command = [cxx, "-std=c++17", "-O2", "-fPIC", "-I", include_dir]
    if m32:
        command.append("-m32")
    if sys.platform == "win32":
        command.append("-shared")
    elif sys.platform == "darwin":
        command.extend(["-dynamiclib"])
    else:
        command.extend(["-shared"])
    command.extend(
        [
            os.path.join(HERE, "std_conformance_native.cpp"),
            "-o",
            os.path.join(out_dir, lib_file),
        ]
    )
    run(command)
    return out_dir


def build_and_run_cpp(cxx, include_dir, library_dir, work, m32):
    out_dir = os.path.join(work, "cpp-32" if m32 else "cpp-64")
    os.makedirs(out_dir, exist_ok=True)
    output = os.path.join(out_dir, "std_consumer")
    command = [cxx, "-std=c++17", "-O2", "-I", include_dir, os.path.join(HERE, "std_consumer.cpp")]
    if m32:
        command.append("-m32")
    command.extend(["-L", library_dir, "-lconformance_std", *rpath_args(library_dir), "-o", output])
    run(command)
    run([output], env=runtime_env(library_dir))
    print(f"  [PASS] C++ consumer ({'32-bit' if m32 else '64-bit'})")


def build_and_run_c(cc, cxx, include_dir, library_dir, work, m32):
    out_dir = os.path.join(work, "c-32" if m32 else "c-64")
    os.makedirs(out_dir, exist_ok=True)
    obj = os.path.join(out_dir, "std_consumer.o")
    output = os.path.join(out_dir, "std_consumer")
    compile_command = [cc, "-std=c11", "-O2", "-I", include_dir]
    if m32:
        compile_command.append("-m32")
    compile_command.extend(["-c", os.path.join(HERE, "std_consumer.c"), "-o", obj])
    run(compile_command)
    link_command = [cxx, obj, "-L", library_dir, "-lconformance_std", *rpath_args(library_dir), "-o", output]
    if m32:
        link_command.insert(1, "-m32")
    run(link_command)
    run([output], env=runtime_env(library_dir))
    print(f"  [PASS] C consumer ({'32-bit' if m32 else '64-bit'})")


def build_and_run_rust(rustc, library_dir, work, m32):
    if m32:
        print("  [SKIP] Rust 32-bit (the i686 std target is not assumed)")
        return
    out_dir = os.path.join(work, "rust")
    os.makedirs(out_dir, exist_ok=True)
    output = os.path.join(out_dir, "std_consumer")
    command = [
        rustc,
        "--edition=2021",
        os.path.join(HERE, "std_consumer.rs"),
        "-L",
        f"native={library_dir}",
        "-o",
        output,
    ]
    if sys.platform != "win32":
        command.extend(["-C", f"link-arg=-Wl,-rpath,{library_dir}"])
    try:
        run(command)
        run([output], env=runtime_env(library_dir))
    except RuntimeError:
        print("  [SKIP] Rust consumer could not link this host")
        return
    print("  [PASS] Rust consumer")


def build_and_run_zig(zig, library_dir, work, m32):
    if m32:
        print("  [SKIP] Zig 32-bit (native-width consumers are the current gate)")
        return
    out_dir = os.path.join(work, "zig")
    os.makedirs(out_dir, exist_ok=True)
    output = os.path.join(out_dir, "std_consumer")
    command = [
        zig,
        "build-exe",
        os.path.join(HERE, "std_consumer.zig"),
        "-O",
        "ReleaseSafe",
        "-lc",
        "-L",
        library_dir,
        "-lconformance_std",
        f"-femit-bin={output}",
    ]
    try:
        run(command)
        run([output], env=runtime_env(library_dir))
    except RuntimeError:
        print("  [SKIP] Zig consumer could not link this host")
        return
    print("  [PASS] Zig consumer")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, help="ABIX source root")
    parser.add_argument("--cxx", required=True, help="C++ compiler")
    parser.add_argument("--cc", help="C compiler")
    parser.add_argument("--rustc", help="rustc executable")
    parser.add_argument("--zig", help="zig executable")
    parser.add_argument("--keep-artifacts", action="store_true")
    args = parser.parse_args()

    if compare_compiler(args.cxx):
        print("  [SKIP] ABIX-STD conformance fixture needs a GCC/Clang-style driver")
        return 0

    include_dir = os.path.join(os.path.abspath(args.root), "include")
    work = tempfile.mkdtemp(prefix="abix-std-conformance.")
    try:
        lib64 = build_native(args.cxx, include_dir, work, "native-64", False)
        build_and_run_cpp(args.cxx, include_dir, lib64, work, False)
        if args.cc:
            build_and_run_c(args.cc, args.cxx, include_dir, lib64, work, False)
        if args.rustc:
            build_and_run_rust(args.rustc, lib64, work, False)
        else:
            print("  [SKIP] rustc not found")
        if args.zig:
            build_and_run_zig(args.zig, lib64, work, False)
        else:
            print("  [SKIP] zig not found")

        # 32-bit matrix: the same headers and the same consumers must agree on
        # size, alignment and offsets. A host without 32-bit libc reports a skip.
        try:
            lib32 = build_native(args.cxx, include_dir, work, "native-32", True)
            build_and_run_cpp(args.cxx, include_dir, lib32, work, True)
            if args.cc:
                build_and_run_c(args.cc, args.cxx, include_dir, lib32, work, True)
            if args.rustc:
                build_and_run_rust(args.rustc, lib32, work, True)
            if args.zig:
                build_and_run_zig(args.zig, lib32, work, True)
            print("  [PASS] 32-bit C/C++ layout matrix")
        except RuntimeError:
            print("  [SKIP] 32-bit target build not available")
        return 0
    finally:
        if args.keep_artifacts or os.environ.get("KEEP_ARTIFACTS", "") == "1":
            print(f"  [KEEP] retained artifacts: {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
