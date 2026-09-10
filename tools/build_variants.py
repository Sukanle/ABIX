#!/usr/bin/env python3

from __future__ import annotations

import os
import shutil
import argparse
import subprocess
import sys
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
    return 'clang' in target.lower() or os.path.basename(target).startswith('clang')

def build_one(compiler: str, tag: str, build_type: str) -> None:
    out_dir = PROJECT_ROOT / "build" / build_type / "variants" / tag 
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
        "-std=c++17", "-shared", "-fPIC", *opts,
        *map(lambda dir: f"-I{dir}", INCLUDE_DIR), "-o", str(out), str(src),
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

    if sys.platform == "darwin" and (build_type == "Debug" or build_type == "RelWithDebInfo"):
        subprocess.run(["dsymutil", str(out), "-o", str(dSYM)])
        print(f"    Generated .dSYM: {dSYM}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Build variants of the tool.")
    parser.add_argument("--build-type", default="Release", help="Build type (Release or Debug)")
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

    print("== Variant build complete ==")


if __name__ == "__main__":
    main()
