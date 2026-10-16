#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
INCLUDE_DIR = {PROJECT_ROOT, PROJECT_ROOT / "mics"}

GCC_FALLBACKS = [
    r"g++.exe",
    r"g++",
]
CLANG_FALLBACKS = [
    r"clang++.exe",
    r"clang++",
]

# Windows-only cross-toolchain variant built with the MSVC toolchain (cl.exe).
MSVC_TAG = "msvc_x64"
# (dll name, source name, extra link libraries)
MSVC_VARIANTS = [
    ("version_dll", "version_dll", []),
    ("resource_dll", "resource_dll", ["ws2_32.lib"]),
    ("interface_dll", "interface_dll", []),
    ("owner_dll", "owner_dll", []),
]
# Entries injected by an MSYS2/MinGW shell must not leak into the MSVC build:
# the MinGW headers/libraries conflict with the Visual C++ runtime.
MSVC_STRIP = re.compile(r"msys2|mingw|ucrt64", re.IGNORECASE)
MSVC_PATH_STRIP = re.compile(r"msys2-data", re.IGNORECASE)


def find_compiler(name: str) -> str | None:
    path = shutil.which(name)
    if path:
        return path
    return None


def is_symlink_to_clang(path: str) -> bool:
    if not os.path.islink(path):
        return False
    target = os.readlink(path)
    # Check if target is clang (or contains 'clang' in its name)
    return "clang" in target.lower() or os.path.basename(target).startswith("clang")


def variants_dir(build_type: str) -> Path:
    """Stage directory for the cross-toolchain variants.

    The Catch2 suites look for `./variants/<tag>/<dll>/...` relative to their
    working directory, which is `<build>/<build_type>/bin` for both ctest and
    tools/build.py.
    """
    return PROJECT_ROOT / "build" / build_type / "bin" / "variants"


def build_one(compiler: str, tag: str, build_type: str) -> None:
    out_dir = variants_dir(build_type) / tag / "version_dll"
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / "version_dll.dll"
    src = PROJECT_ROOT / "dlls" / "version_dll.cpp"
    opts = []
    dSYM = out_dir / "version_dll.dSYM"

    if build_type == "Release":
        opts.append("-O2")
    else:
        opts.append("-g")
        opts.append("-O0")

    print(f"== [{tag}] {compiler} ==")
    cmd = [
        compiler,
        "-std=c++17",
        "-shared",
        "-fPIC",
        *opts,
        *map(lambda dir: f"-I{dir}", INCLUDE_DIR),
        "-o",
        str(out),
        str(src),
    ]
    print(f"  \033[90m{' '.join(cmd)}\033[0m")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.stdout:
        for line in result.stdout.splitlines():
            print(f"    {line}")
    if result.stderr:
        for line in result.stderr.splitlines():
            print(f"    {line}")
    if result.returncode != 0:
        raise SystemExit(f"[{tag}] Compilation failed")
    print(f"    Generated: {out}")

    if sys.platform == "darwin" and (
        build_type == "Debug" or build_type == "RelWithDebInfo"
    ):
        subprocess.run(["dsymutil", str(out), "-o", str(dSYM)])
        print(f"    Generated .dSYM: {dSYM}")


# ---------------------------------------------------------------------------
# MSVC cross-toolchain variants (Windows only).
#
# Mirrors the previous tools/build_msvc_variants.ps1: locate Visual Studio with
# vswhere, enter its developer environment, drop the MSYS2/MinGW entries that
# would shadow the Visual C++ runtime, then compile the DLLs with cl.exe.
# ---------------------------------------------------------------------------
def find_visual_studio(explicit_path: str | None) -> str:
    """Return the Visual Studio installation path (newest, or the given one)."""
    if explicit_path:
        return explicit_path
    program_files_x86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = (
        Path(program_files_x86)
        / "Microsoft Visual Studio"
        / "Installer"
        / "vswhere.exe"
    )
    if not vswhere.is_file():
        raise FileNotFoundError(f"vswhere.exe not found: {vswhere}")
    result = subprocess.run(
        [str(vswhere), "-latest", "-property", "installationPath"],
        capture_output=True,
        text=True,
        errors="replace",
    )
    lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    if result.returncode != 0 or not lines:
        raise RuntimeError("vswhere could not locate a Visual Studio installation")
    return lines[0]


def msvc_environment(vs_path: str) -> dict[str, str]:
    """Return the x64 developer environment produced by VsDevCmd.bat.

    The batch file is executed through a temporary script so that cmd.exe
    receives an unescaped command line, then `set` dumps the environment.
    """
    dev_cmd = Path(vs_path) / "Common7" / "Tools" / "VsDevCmd.bat"
    if not dev_cmd.is_file():
        raise FileNotFoundError(f"VsDevCmd.bat not found: {dev_cmd}")

    with tempfile.NamedTemporaryFile("w", suffix=".bat", delete=False) as handle:
        handle.write("@echo off\r\n")
        handle.write(f'call "{dev_cmd}" -arch=x64 -host_arch=x64 -no_logo\r\n')
        handle.write("set\r\n")
        script = handle.name

    try:
        result = subprocess.run(
            ["cmd", "/c", script],
            capture_output=True,
            text=True,
            errors="replace",
        )
    finally:
        os.unlink(script)

    # `cmd`'s `set` echoes each variable with whatever case the parent process
    # used (`Path` vs `PATH`), so normalise the names: Windows environment
    # lookup is case-insensitive and the caller indexes INCLUDE/LIB/PATH in
    # upper case. Lines like `=C:=...` (drive-local variables) are skipped.
    environment: dict[str, str] = {}
    for line in result.stdout.splitlines():
        key, separator, value = line.partition("=")
        if not separator or not key:
            continue
        environment[key.upper()] = value
    if result.returncode != 0 or "INCLUDE" not in environment:
        raise RuntimeError("could not obtain the Visual C++ environment")
    return environment


def strip_env_entries(value: str, pattern: re.Pattern[str]) -> str:
    """Return `value` with the PATH-separated entries matching `pattern` removed."""
    return os.pathsep.join(
        entry for entry in value.split(os.pathsep) if entry and not pattern.search(entry)
    )


def which_in_path(path_value: str, name: str) -> str | None:
    for directory in path_value.split(os.pathsep):
        if not directory:
            continue
        candidate = Path(directory) / name
        if candidate.is_file():
            return str(candidate)
    return None


def run_msvc(cmd: list[str], env: dict[str, str]) -> None:
    print(f"  \033[90m{' '.join(cmd)}\033[0m")
    result = subprocess.run(
        cmd, env=env, capture_output=True, text=True, errors="replace"
    )
    for line in (result.stdout or "").splitlines():
        print(f"    {line}")
    for line in (result.stderr or "").splitlines():
        print(f"    {line}")
    if result.returncode != 0:
        raise RuntimeError(f"exit code {result.returncode}")


def build_msvc_variant(
    cl: str,
    env: dict[str, str],
    dll_name: str,
    src_name: str,
    link_libs: list[str],
    build_type: str,
) -> Path:
    out_dir = variants_dir(build_type) / MSVC_TAG / dll_name
    out_dir.mkdir(parents=True, exist_ok=True)
    src = PROJECT_ROOT / "dlls" / f"{src_name}.cpp"
    obj = out_dir / f"{src_name}.obj"
    dll = out_dir / f"{dll_name}.dll"

    print(f"== [{MSVC_TAG}] Compiling {src_name} -> {dll} ==")
    run_msvc(
        [
            cl,
            "/nologo",
            "/TP",
            "/std:c++20",
            "/MD",
            "/utf-8",
            "/EHsc",
            "/O2",
            "/permissive-",
            "/D_AMD64_",
            *[f"/I{d}" for d in (PROJECT_ROOT, PROJECT_ROOT / "mics")],
            "/c",
            str(src),
            f"/Fo{obj}",
        ],
        env,
    )
    run_msvc([cl, "/nologo", str(obj), f"/Fe{dll}", "/LD", *link_libs], env)

    for suffix in (".obj", ".exp", ".lib"):
        (out_dir / f"{src_name}{suffix}").unlink(missing_ok=True)
    print(f"    Generated: {dll}")
    return dll


def build_msvc_variants(vs_path: str | None, build_type: str) -> None:
    """Build the MSVC cross-toolchain variants. Windows only."""
    print("\n== MSVC cross-toolchain variants ==")
    try:
        vs = find_visual_studio(vs_path)
        print(f"  Visual Studio: {vs}")
        environment = msvc_environment(vs)
    except (OSError, RuntimeError) as exc:
        print(f"  [Warning] MSVC variant build skipped: {exc}")
        return

    for key in ("INCLUDE", "LIB"):
        if key in environment:
            environment[key] = strip_env_entries(environment[key], MSVC_STRIP)
    environment["PATH"] = strip_env_entries(
        environment.get("PATH", ""), MSVC_PATH_STRIP
    )

    cl = which_in_path(environment["PATH"], "cl.exe")
    if not cl:
        print("  [Warning] cl.exe not found in the Visual C++ environment, skipping")
        return
    print(f"  cl: {cl}")

    for dll_name, src_name, link_libs in MSVC_VARIANTS:
        try:
            build_msvc_variant(cl, environment, dll_name, src_name, link_libs, build_type)
        except RuntimeError as exc:
            print(
                f"  [Warning] {dll_name} build failed ({exc}); "
                "cross-CRT tests will be skipped"
            )


def main() -> None:
    parser = argparse.ArgumentParser(description="Build variants of the tool.")
    parser.add_argument(
        "--build-type", default="Release", help="Build type (Release or Debug)"
    )
    if sys.platform == "win32":
        parser.add_argument(
            "--with-msvc",
            action="store_true",
            help="Also build MSVC cross-compiler variants (for Test 13)",
        )
        parser.add_argument(
            "--vs-path",
            default="",
            help="Visual Studio installation path (use with --with-msvc)",
        )
    args = parser.parse_args()

    gnu = find_compiler("g++")
    clang = find_compiler("clang++")

    if sys.platform == "darwin" and gnu:
        if is_symlink_to_clang(gnu):
            print("  [macOS] g++ is a symlink to clang, skipping it.")
            gnu = None

    if not gnu and not clang:
        raise SystemExit("No suitable C++ compiler found (g++ or clang++).")

    if gnu:
        build_one(gnu, "tool_x", args.build_type)
    if clang:
        build_one(clang, "tool_y", args.build_type)

    if sys.platform == "win32" and args.with_msvc:
        build_msvc_variants(args.vs_path or None, args.build_type)

    print("== Variant build complete ==")


if __name__ == "__main__":
    main()
