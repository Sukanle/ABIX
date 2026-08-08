#!/usr/bin/env python3

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent
INCLUDE_DIR = {PROJECT_ROOT, PROJECT_ROOT / "Reflection"}
DLL_SRC_DIR = PROJECT_ROOT / "dlls"
VARIANT_DIR = PROJECT_ROOT / "variants"

GCC_FALLBACKS = [
    r"G:\msys2-data\ucrt64\bin\g++.exe",
    r"C:\msys64\ucrt64\bin\g++.exe",
]
CLANG_FALLBACKS = [
    r"G:\msys2-data\ucrt64\bin\clang++.exe",
    r"C:\msys64\ucrt64\bin\clang++.exe",
]


def find_compiler(name: str, fallbacks: list[str]) -> str | None:
    path = shutil.which(name)
    if path:
        return path
    for fb in fallbacks:
        if os.path.isfile(fb):
            return fb
    return None


def build_one(compiler: str, tag: str) -> None:
    out_dir = VARIANT_DIR / tag / "version_dll"
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / "version_dll.dll"
    src = DLL_SRC_DIR / "version_dll.cpp"

    print(f"== [{tag}] {compiler} ==")
    cmd = [
        compiler,
        "-std=c++20", "-shared", "-fPIC", "-O2",
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

    stage = PROJECT_ROOT / "build" / "plugins" / "variants"
    dst = stage / tag / "version_dll"
    dst.mkdir(parents=True, exist_ok=True)
    shutil.copy2(out, dst / "version_dll.dll")
    print(f"    Copied to: {dst}")


def main() -> None:
    gcc = find_compiler("g++.exe", GCC_FALLBACKS)
    clang = find_compiler("clang++.exe", CLANG_FALLBACKS)

    if not gcc and not clang:
        raise SystemExit("g++ or clang++ compiler not found")

    if gcc:
        build_one(gcc, "tool_x")
    if clang:
        build_one(clang, "tool_y")

    print("== Variant build complete ==")


if __name__ == "__main__":
    main()