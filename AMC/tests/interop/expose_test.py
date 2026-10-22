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

    A MinGW DLL exports nothing unless asked, but the generated thunks rely on
    the import library, so Windows builds export every symbol and emit the
    `lib<base>.dll.a` that rustc links against.
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


def toml_path(path: str) -> str:
    """A path safe to embed in a TOML string literal.

    TOML treats a backslash as an escape introducer, so a Windows path like
    `F:\\Code\\cmdly` would fail to parse (`unknown escape sequence '\\C'`).
    Forward slashes are accepted by both TOML and the C++ compiler.
    """
    return path.replace("\\", "/")


# `--keep-artifacts`, or KEEP_ARTIFACTS=1 in the environment (set by the CMake
# option SKL_ABIX_KEEP_TEST_ARTIFACTS), retains the temporary working directory.
if "--keep-artifacts" in sys.argv:
    os.environ["KEEP_ARTIFACTS"] = "1"
KEEP_ARTIFACTS = os.environ.get("KEEP_ARTIFACTS", "") == "1"


def main() -> int:
    argv = [arg for arg in sys.argv[1:] if arg != "--keep-artifacts"]
    if not argv:
        print("usage: expose_test.py <amc_bin_dir> [source_root]", file=sys.stderr)
        return 2
    bin_dir = os.path.abspath(argv[0])
    source_root = os.path.abspath(argv[1]) if len(argv) > 1 else os.path.abspath(
        os.path.join(SCRIPT_DIR, "..", "..", ".."))
    amc = os.path.join(bin_dir, exe_name("amc"))
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

    work = os.environ.get("AMC_INTEROP_WORK_DIR") or tempfile.mkdtemp(prefix="amc-expose.")
    fixture = os.path.join(SCRIPT_DIR, "expose")
    for name in ("counter.hpp", "counter.abic.toml", "rust_consumer.rs", "cpp_consumer.cpp"):
        shutil.copy(os.path.join(fixture, name), os.path.join(work, name))

    # 1. Describe the class and generate its thunks.
    r = run([amc, "build", "-c", os.path.join(work, "counter.abic.toml"), "-B", os.path.join(work, "out")])
    if not check(r.returncode == 0, "amc build generated the metadata and thunks", (r.stdout + r.stderr)[-400:]):
        return 1
    thunks = os.path.join(work, "abix_thunks.cpp")
    check(os.path.isfile(thunks), "abix_thunks.cpp was generated")
    abix = os.path.join(work, "out", "counter_expose.abix")

    # 2. Compile the generated thunks into the producing module.
    module = module_path(work, "counter")
    r = run([cxx, "-std=c++17", "-fPIC", "-shared", "-O2", thunks, "-I" + source_root, "-o", module]
            + module_flags(work, "counter"))
    if not check(r.returncode == 0, "the generated thunks compile", r.stderr[-400:]):
        return 1
    if IS_WINDOWS:
        # `nm` cannot read a PE DLL's export table; the import library carries
        # the exported names instead.
        nm_args = ["nm", import_lib(work, "counter")]
    elif IS_MACOS:
        nm_args = ["nm", "-gU", module]
    else:
        nm_args = ["nm", "-D", module]
    check("abix_module_query_v1" in subprocess.run(nm_args, capture_output=True, text=True).stdout,
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
    consumer = os.path.join(work, exe_name("rust_consumer"))
    r = run([rustc, "--edition", "2021", os.path.join(work, "rust_consumer.rs"), "-o", consumer]
            + rustc_link(work, "counter"))
    if not check(r.returncode == 0, "rustc compiled the consumer", r.stderr[-400:]):
        return 1
    env = runtime_env(work)
    r = run([consumer], env=env)
    check(r.returncode == 0 and "expose counter" in r.stdout,
          "Rust created, called and destroyed the C++ object", (r.stdout + r.stderr)[-400:])

    # 5/8/9: discovery through the fixed entry, binding once, and a module lease
    # that keeps the module loaded while the object is alive.
    cpp_consumer = os.path.join(work, exe_name("cpp_consumer"))
    r = run([cxx, "-std=c++17", os.path.join(work, "cpp_consumer.cpp"), "-I" + source_root, "-o", cpp_consumer])
    if not check(r.returncode == 0, "the registry consumer compiles", r.stderr[-400:]):
        return 1
    r = run([cpp_consumer, module], env=env)
    check(r.returncode == 0 and "expose registry: OK" in r.stdout,
          "registry bus discovered, bound and leased the exposed capabilities", (r.stdout + r.stderr)[-400:])

    # ABIX-STD contract mapping: exposed methods speak SequenceView / StringView
    # / Result, and the contracts are recorded and projected.
    for name in ("std_api.hpp", "std_api.abic.toml", "rust_consumer_std.rs"):
        shutil.copy(os.path.join(fixture, name), os.path.join(work, name))
    # The fixture includes `ABIX/STD/...` by its include root; add the source
    # root to the import flags so neither the header nor the generated thunks
    # need an absolute include path.
    std_config = os.path.join(work, "std_api.abic.toml")
    text = open(std_config, encoding="utf-8").read().replace(
        '"-std=c++17"', '"-std=c++17", "-I' + toml_path(source_root) + '"')
    open(std_config, "w", encoding="utf-8").write(text)
    r = run([amc, "build", "-c", std_config, "-B", os.path.join(work, "std_out")])
    if not check(r.returncode == 0, "amc build for the ABIX-STD api", (r.stdout + r.stderr)[-400:]):
        return 1
    std_module = module_path(work, "metrics")
    r = run([cxx, "-std=c++17", "-fPIC", "-shared", "-O2", os.path.join(work, "std_thunks.cpp"),
             "-I" + source_root, "-o", std_module] + module_flags(work, "metrics"))
    if not check(r.returncode == 0, "the ABIX-STD thunks compile", r.stderr[-400:]):
        return 1
    std_gen = os.path.join(work, "std_gen")
    os.makedirs(std_gen, exist_ok=True)
    r = run([amc, "generate", os.path.join(work, "std_out", "expose_std.abix"), "-l", "rust", "-o", std_gen])
    if not check(r.returncode == 0, "amc generate for the ABIX-STD api", (r.stdout + r.stderr)[-400:]):
        return 1
    std_consumer = os.path.join(work, exe_name("rust_consumer_std"))
    # rust_consumer_std.rs includes `gen/amc_generated.rs`; point it at the std gen.
    std_consumer_src = os.path.join(work, "rust_consumer_std.rs")
    text = open(std_consumer_src, encoding="utf-8").read().replace("gen/amc_generated.rs", "std_gen/amc_generated.rs")
    open(std_consumer_src, "w", encoding="utf-8").write(text)
    r = run([rustc, "--edition", "2021", std_consumer_src, "-o", std_consumer]
            + rustc_link(work, "metrics"))
    if not check(r.returncode == 0, "rustc compiled the ABIX-STD consumer", r.stderr[-400:]):
        return 1
    r = run([std_consumer], env=env)
    check(r.returncode == 0 and "expose std: OK" in r.stdout,
          "SequenceView / StringView / Result mapped through the exposed API", (r.stdout + r.stderr)[-400:])

    # Native↔ABIX-STD parameter adaptation: a declared [[adapt]] maps a native
    # STL parameter type onto an ABIX-STD boundary type, so the frontend exposes
    # a method that takes std::string / std::vector / std::span and generates
    # the marshalling adapter instead of refusing it.
    for name in ("native_api.hpp", "native_api.abic.toml", "rust_consumer_native.rs"):
        shutil.copy(os.path.join(fixture, name), os.path.join(work, name))
    r = run([amc, "build", "-c", os.path.join(work, "native_api.abic.toml"), "-B", os.path.join(work, "nat_out")])
    if not check(r.returncode == 0, "amc build for the native adaptation api", (r.stdout + r.stderr)[-400:]):
        return 1
    native_thunks = open(os.path.join(work, "native_thunks.cpp"), encoding="utf-8").read()
    check("abix_str_utf8" in native_thunks and "std::string(" in native_thunks,
          "a std::string parameter becomes a StringView with a generated adapter")
    check("abix_span_i32" in native_thunks and "std::vector<" in native_thunks,
          "a std::vector parameter becomes a SequenceView with a generated adapter")
    nat_module = module_path(work, "native")
    r = run([cxx, "-std=c++20", "-fPIC", "-shared", "-O2", os.path.join(work, "native_thunks.cpp"),
             "-I" + source_root, "-o", nat_module] + module_flags(work, "native"))
    if not check(r.returncode == 0, "the native adaptation thunks compile", r.stderr[-400:]):
        return 1
    nat_gen = os.path.join(work, "nat_gen")
    os.makedirs(nat_gen, exist_ok=True)
    r = run([amc, "generate", os.path.join(work, "nat_out", "native_api.abix"), "-l", "rust", "-o", nat_gen])
    if not check(r.returncode == 0, "amc generate for the native adaptation api", (r.stdout + r.stderr)[-400:]):
        return 1
    nat_consumer_src = os.path.join(work, "rust_consumer_native.rs")
    text = open(nat_consumer_src, encoding="utf-8").read().replace("gen/amc_generated.rs", "nat_gen/amc_generated.rs")
    open(nat_consumer_src, "w", encoding="utf-8").write(text)
    nat_consumer = os.path.join(work, exe_name("rust_consumer_native"))
    r = run([rustc, "--edition", "2021", nat_consumer_src, "-o", nat_consumer]
            + rustc_link(work, "native"))
    if not check(r.returncode == 0, "rustc compiled the native adaptation consumer", r.stderr[-400:]):
        return 1
    r = run([nat_consumer], env=env)
    check(r.returncode == 0 and "expose native: OK" in r.stdout,
          "native string/vector/span parameters adapted onto ABIX-STD views", (r.stdout + r.stderr)[-400:])

    # Interface contracts: [[interface]] + [[implements]] project to a C++ header
    # carrying the identity, a Traits struct and an UniqueInterfaceProxy-derived class,
    # so a consumer can declare and call the interface without a private vtable.
    for name in ("shapes.hpp", "shapes.abic.toml"):
        shutil.copy(os.path.join(fixture, name), os.path.join(work, name))
    r = run([amc, "build", "-c", os.path.join(work, "shapes.abic.toml"), "-B", os.path.join(work, "shapes_out")])
    if check(r.returncode == 0, "amc build for the interface contract", (r.stdout + r.stderr)[-400:]):
        iface_header = os.path.join(work, "shapes_iface.hpp")
        r = run([amc, "generate", os.path.join(work, "shapes_out", "shapes.abix"), "-l", "cpp", "-o", iface_header])
        if check(r.returncode == 0, "amc generate -l cpp for the interface contract", (r.stdout + r.stderr)[-400:]):
            header = open(iface_header, encoding="utf-8").read()
            check("kShapeInterface" in header and "struct ShapeTraits" in header and "Shape_shape_area_fn" in header,
                  "the interface identity, Traits and method typedef were emitted")
            check("class Shape : public ::skl::abix::poly::UniqueInterfaceProxy<ShapeTraits>" in header,
                  "a handle interface projects to an UniqueInterfaceProxy-derived class")
            proxy_src = os.path.join(work, "shapes_proxy.cpp")
            open(proxy_src, "w", encoding="utf-8").write(
                '#include "shapes.hpp"\n'
                '#include "shapes_iface.hpp"\n'
                'int main() {\n'
                '    static_assert(amc_generated::ShapeTraits::slots.shape_area == 0);\n'
                '    static_assert(amc_generated::ShapeTraits::signatures.size() == 1);\n'
                '    return amc_generated::ShapeTraits::version == 1 ? 0 : 1;\n'
                '}\n')
            proxy_bin = os.path.join(work, exe_name("shapes_proxy"))
            r = run([cxx, "-std=c++17", "-I" + source_root, "-I" + work, proxy_src, "-o", proxy_bin])
            if check(r.returncode == 0, "the generated interface proxy header compiles", r.stderr[-400:]):
                r = run([proxy_bin], env=env)
                check(r.returncode == 0, "the generated interface proxy header runs")

    # Real-project comparison: expose a type from cmdly. Auto-expose covers its
    # representable surface; a std::string method is refused pending a contract.
    cmdly_root = os.environ.get("CMDLY_ROOT") or os.path.expanduser("~/Code/cmdly")
    cmdly_include = os.path.join(cmdly_root, "libcmdly", "include")
    cmdly_lib = None
    for candidate in ("build/Release/libcmdly", "build/libcmdly", "build/lib", "lib"):
        directory = os.path.join(cmdly_root, candidate)
        for name in ("libcmdly.so", "libcmdly.dylib", "libcmdly.dll", "cmdly.dll"):
            if os.path.isfile(os.path.join(directory, name)):
                cmdly_lib = directory
                break
        if cmdly_lib:
            break
    if not os.path.isdir(cmdly_include) or not cmdly_lib:
        print("  [SKIP] cmdly real-project comparison (set CMDLY_ROOT to enable)")
    else:
        # cmdly's headers include <fmt/...>; a Homebrew fmt is not on the default
        # search path, so ask pkg-config for its flags (same as cmdly_test.py).
        fmt_cflags = []
        fmt_probe = run(["pkg-config", "--cflags", "fmt"])
        if fmt_probe.returncode == 0:
            fmt_cflags = [flag.replace("\\", "/") for flag in fmt_probe.stdout.split()]
        fmt_toml = "".join(', "%s"' % flag for flag in fmt_cflags)
        # GNU ld-only option for transitive deps; the Apple and PE linkers
        # reject it.
        rpath_link = [] if (IS_MACOS or IS_WINDOWS) else [
            "-C", "link-arg=-Wl,-rpath-link," + cmdly_lib]
        key_header = os.path.join(cmdly_include, "cmdly", "key.h")
        key_config = os.path.join(work, "cmdly_key.abic.toml")
        open(key_config, "w", encoding="utf-8").write(f"""[package]
name = "cmdly_key"
version = "1.0"
[[import]]
language = "cpp"
flags = ["-std=c++17", "-I{toml_path(cmdly_include)}"{fmt_toml}]
files = ["{toml_path(key_header)}"]
symbols = ["cmdly::Key"]
[[expose]]
type = "cmdly::Key"
methods = ["code", "isPrintable"]
constructor = ["char"]
[thunks]
output = "cmdly_thunks.cpp"
[[export]]
output = "cmdly_key.abix"
""")
        r = run([amc, "build", "-c", key_config, "-B", os.path.join(work, "cmdly_out")])
        ok = check(r.returncode == 0, "cmdly::Key exposed automatically", (r.stdout + r.stderr)[-400:])
        if ok:
            key_module = module_path(work, "cmdlykey")
            key_link = ["-L" + cmdly_lib] + ([] if IS_WINDOWS else ["-Wl,-rpath," + cmdly_lib])
            r = run([cxx, "-std=c++17", "-fPIC", "-shared", "-O2", os.path.join(work, "cmdly_thunks.cpp"),
                     "-I" + source_root, "-I" + cmdly_include, "-o", key_module]
                    + key_link + fmt_cflags + module_flags(work, "cmdlykey"))
            if check(r.returncode == 0, "cmdly thunks compile", r.stderr[-400:]):
                key_gen = os.path.join(work, "cmdly_gen")
                os.makedirs(key_gen, exist_ok=True)
                r = run([amc, "generate", os.path.join(work, "cmdly_out", "cmdly_key.abix"), "-l", "rust",
                         "-o", key_gen])
                check(r.returncode == 0, "cmdly capabilities project to Rust", (r.stdout + r.stderr)[-400:])
                key_src = os.path.join(work, "rust_consumer_key.rs")
                shutil.copy(os.path.join(fixture, "rust_consumer_key.rs"), key_src)
                key_text = open(key_src, encoding="utf-8").read().replace(
                    "gen/amc_generated.rs", "cmdly_gen/amc_generated.rs")
                open(key_src, "w", encoding="utf-8").write(key_text)
                kenv = runtime_env(work, [cmdly_lib])
                key_bin = os.path.join(work, exe_name("rust_consumer_key"))
                r = run([rustc, "--edition", "2021", key_src, "-o", key_bin]
                        + rustc_link(work, "cmdlykey", [cmdly_lib]) + rpath_link)
                if check(r.returncode == 0, "cmdly consumer compiles", r.stderr[-400:]):
                    r = run([key_bin], env=kenv)
                    check(r.returncode == 0 and "expose cmdly: OK" in r.stdout,
                          "Rust drove the real cmdly Key", (r.stdout + r.stderr)[-400:])

        # Without a declared [[adapt]] a std::string method is refused: an STL
        # type must not cross the boundary raw (ABI-SPEC §18 requires an
        # explicit mapping).
        ins_config = os.path.join(work, "cmdly_insert.abic.toml")
        ins_header = os.path.join(cmdly_include, "cmdly", "history.h")
        open(ins_config, "w", encoding="utf-8").write(f"""[package]
name = "cmdly_insert"
version = "1.0"
[[import]]
language = "cpp"
flags = ["-std=c++17", "-I{toml_path(cmdly_include)}"{fmt_toml}]
files = ["{toml_path(ins_header)}"]
symbols = ["cmdly::History"]
[[expose]]
type = "cmdly::History"
methods = ["insert"]
constructor = ["unsigned long"]
[thunks]
output = "insert_thunks.cpp"
[[export]]
output = "cmdly_insert.abix"
""")
        r = run([amc, "build", "-c", ins_config, "-B", os.path.join(work, "insert_out")])
        check(r.returncode != 0 and "needs a contract" in (r.stdout + r.stderr),
              "a std::string method is refused without an explicit adaptation", (r.stdout + r.stderr)[-400:])

        # With a declared [[adapt]] the same method is auto-exposed: the thunk
        # takes the StringView boundary type and marshals it back onto
        # std::string. A concrete derived type drives the inherited method.
        hist_config = os.path.join(work, "cmdly_history.abic.toml")
        open(hist_config, "w", encoding="utf-8").write(f"""[package]
name = "cmdly_history"
version = "1.0"
[[import]]
language = "cpp"
flags = ["-std=c++17", "-I{toml_path(cmdly_include)}"{fmt_toml}]
files = ["{toml_path(ins_header)}"]
symbols = ["cmdly::MemoryHistory"]
[[adapt]]
native = "std::string"
target = "abix_str_utf8"
[[expose]]
type = "cmdly::MemoryHistory"
methods = ["insert", "length"]
constructor = []
[thunks]
output = "history_thunks.cpp"
[[export]]
output = "cmdly_history.abix"
""")
        r = run([amc, "build", "-c", hist_config, "-B", os.path.join(work, "hist_out")])
        ok = check(r.returncode == 0, "cmdly::MemoryHistory::insert exposed after an adaptation",
                   (r.stdout + r.stderr)[-400:])
        if ok:
            history_thunks = open(os.path.join(work, "history_thunks.cpp"), encoding="utf-8").read()
            check("abix_str_utf8 line" in history_thunks and "std::string(line.data, line.len)" in history_thunks,
                  "the inherited insert takes a StringView and adapts it")
            history_module = module_path(work, "cmdlyhistory")
            history_link = ["-L" + cmdly_lib] + ([] if IS_WINDOWS else ["-Wl,-rpath," + cmdly_lib])
            r = run([cxx, "-std=c++17", "-fPIC", "-shared", "-O2", os.path.join(work, "history_thunks.cpp"),
                     "-I" + source_root, "-I" + cmdly_include, "-o", history_module]
                    + history_link + fmt_cflags + module_flags(work, "cmdlyhistory"))
            if check(r.returncode == 0, "cmdly history thunks compile", r.stderr[-400:]):
                hist_gen = os.path.join(work, "hist_gen")
                os.makedirs(hist_gen, exist_ok=True)
                r = run([amc, "generate", os.path.join(work, "hist_out", "cmdly_history.abix"), "-l", "rust",
                         "-o", hist_gen])
                if check(r.returncode == 0, "cmdly history capabilities project to Rust", (r.stdout + r.stderr)[-400:]):
                    hist_src = os.path.join(work, "rust_consumer_history.rs")
                    shutil.copy(os.path.join(fixture, "rust_consumer_history.rs"), hist_src)
                    text = open(hist_src, encoding="utf-8").read().replace(
                        "gen/amc_generated.rs", "hist_gen/amc_generated.rs")
                    open(hist_src, "w", encoding="utf-8").write(text)
                    hist_bin = os.path.join(work, exe_name("rust_consumer_history"))
                    r = run([rustc, "--edition", "2021", hist_src, "-o", hist_bin]
                            + rustc_link(work, "cmdlyhistory", [cmdly_lib]) + rpath_link)
                    if check(r.returncode == 0, "cmdly history consumer compiles", r.stderr[-400:]):
                        hvenv = runtime_env(work, [cmdly_lib])
                        r = run([hist_bin], env=hvenv)
                        check(r.returncode == 0 and "expose history: OK" in r.stdout,
                              "Rust drove the real cmdly insert through a StringView", (r.stdout + r.stderr)[-400:])

    if KEEP_ARTIFACTS:
        print("  [KEEP] retained artifacts: " + work)
    else:
        shutil.rmtree(work, ignore_errors=True)
    print("=== automatic capability generation: %d passed, %d failed ===" % (passed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
