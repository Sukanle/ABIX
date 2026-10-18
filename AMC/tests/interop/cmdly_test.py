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


def exe_name(base: str) -> str:
    """Append `.exe` on Windows (including MSYS2/Cygwin Python), else as-is."""
    return base + ".exe" if sys.platform == "win32" else base


IS_WINDOWS = sys.platform == "win32"
IS_MACOS = sys.platform == "darwin"


def module_path(work: str, base: str) -> str:
    """Output path of the shared module `base` (libX.so / libX.dylib / X.dll)."""
    if IS_MACOS:
        return os.path.join(work, "lib" + base + ".dylib")
    if IS_WINDOWS:
        return os.path.join(work, base + ".dll")
    return os.path.join(work, "lib" + base + ".so")


def import_lib(work: str, base: str) -> str:
    """Import library emitted next to the Windows shared module `base`."""
    return os.path.join(work, "lib" + base + ".dll.a")


def module_flags(work: str, base: str) -> list:
    """Extra linker flags used to build the shared module `base`.

    A MinGW DLL exports nothing unless asked, so Windows builds export every
    symbol and emit the `lib<base>.dll.a` that rustc links against.
    """
    if IS_WINDOWS:
        return ["-Wl,--export-all-symbols", "-Wl,--out-implib," + import_lib(work, base)]
    return []


def rustc_link(work: str, base: str, search_dirs=()) -> list:
    """rustc arguments that link a consumer against the shared module `base`."""
    args = ["-L", "native=" + work, "-l", "dylib=" + base]
    if IS_WINDOWS:
        for directory in search_dirs:
            args += ["-L", "native=" + directory]
        return args
    for directory in (work, *search_dirs):
        args += ["-C", "link-arg=-Wl,-rpath," + directory]
    return args


def runtime_env(work: str, search_dirs=()) -> dict:
    """Environment that puts the module directories on the loader search path."""
    env = dict(os.environ)
    dirs = [work, *search_dirs]
    if IS_WINDOWS:
        env["PATH"] = os.pathsep.join(dirs + [env.get("PATH", "")])
    else:
        env["LD_LIBRARY_PATH"] = os.pathsep.join(dirs + [env.get("LD_LIBRARY_PATH", "")])
        env["DYLD_LIBRARY_PATH"] = os.pathsep.join(dirs + [env.get("DYLD_LIBRARY_PATH", "")])
    return env


# `--keep-artifacts`, or KEEP_ARTIFACTS=1 in the environment (set by the CMake
# option SKL_ABIX_KEEP_TEST_ARTIFACTS), retains the temporary working directory.
if "--keep-artifacts" in sys.argv:
    os.environ["KEEP_ARTIFACTS"] = "1"
KEEP_ARTIFACTS = os.environ.get("KEEP_ARTIFACTS", "") == "1"


def find_cmdly():
    root = os.environ.get("CMDLY_ROOT") or os.path.expanduser("~/Code/cmdly")
    include = os.path.join(root, "libcmdly", "include")
    for candidate in ("build/Release/libcmdly", "build/libcmdly", "build/lib", "lib"):
        directory = os.path.join(root, candidate)
        for name in ("libcmdly.so", "libcmdly.dylib", "libcmdly.dll", "cmdly.dll"):
            if os.path.isfile(os.path.join(directory, name)):
                return root, include, directory
    return root, include, None


def main() -> int:
    argv = [arg for arg in sys.argv[1:] if arg != "--keep-artifacts"]
    if not argv:
        print("usage: cmdly_test.py <amc_bin_dir> [source_root]", file=sys.stderr)
        return 2
    bin_dir = os.path.abspath(argv[0])
    amc = os.path.join(bin_dir, exe_name("amc"))
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

    work = os.environ.get("AMC_INTEROP_WORK_DIR") or tempfile.mkdtemp(prefix="amc-cmdly.")
    fixture = os.path.join(SCRIPT_DIR, "cmdly")
    for name in ("cmdly_c.h", "cmdly_c.cpp", "cmdly.abic.toml", "rust_consumer.rs"):
        shutil.copy(os.path.join(fixture, name), os.path.join(work, name))

    facade = module_path(work, "cmdly_c")

    # 1. Build the C facade over cmdly.
    # cmdly's headers include <fmt/...>; a Linux system fmt is on the default
    # search path, but a Homebrew fmt is not, so ask pkg-config for its flags.
    fmt_cflags = []
    fmt_probe = run(["pkg-config", "--cflags", "fmt"])
    if fmt_probe.returncode == 0:
        fmt_cflags = fmt_probe.stdout.split()
    facade_link = ["-L" + cmdly_lib, "-lcmdly"] + ([] if IS_WINDOWS else ["-Wl,-rpath," + cmdly_lib])
    r = run([
        cxx, "-std=c++17", "-fPIC", "-shared", "-O2",
        os.path.join(work, "cmdly_c.cpp"),
        "-I" + cmdly_include,
        "-I" + os.path.join(cmdly_root, "libcmdly", "include"),
        "-o", facade,
    ] + facade_link + fmt_cflags + module_flags(work, "cmdly_c"))
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
    consumer = os.path.join(work, exe_name("rust_consumer"))
    # GNU ld resolves transitive shared-library dependencies at link time;
    # Mach-O and PE ld have no -rpath-link option and do not need it.
    link_args = []
    if not (IS_MACOS or IS_WINDOWS):
        link_args = ["-C", "link-arg=-Wl,-rpath-link," + cmdly_lib]
    r = run([
        rustc, "--edition", "2021", os.path.join(work, "rust_consumer.rs"), "-o", consumer,
    ] + rustc_link(work, "cmdly_c", [cmdly_lib]) + link_args)
    if not check(r.returncode == 0, "rustc compiled the consumer", r.stderr[-400:]):
        return 1

    # 5. Run it.
    env = runtime_env(work, [cmdly_lib])
    r = run([consumer], env=env)
    check(r.returncode == 0 and "cmdly ABIX interop" in r.stdout,
          "the Rust consumer drove cmdly through the ABIX facade", (r.stdout + r.stderr)[-400:])

    if KEEP_ARTIFACTS:
        print("  [KEEP] retained artifacts: " + work)
    else:
        shutil.rmtree(work, ignore_errors=True)
    print("=== cmdly interop: %d passed, %d failed ===" % (passed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
