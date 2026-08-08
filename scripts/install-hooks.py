#!/usr/bin/env python3
"""Install Git hooks for the repository."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path


def run(cmd: list[str], check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, capture_output=True, text=True, check=check)


def has_command(name: str) -> bool:
    return shutil.which(name) is not None


def inside_git_repo() -> bool:
    try:
        run(["git", "rev-parse", "--is-inside-work-tree"])
        return True
    except subprocess.CalledProcessError:
        return False


def main() -> int:
    print("Installing Git Hooks")

    if not has_command("git"):
        print("Error: Git was not found. Please install Git first.", file=sys.stderr)
        return 1

    if not inside_git_repo():
        print("Error: Current directory is not inside a Git repository.", file=sys.stderr)
        return 1

    run(["git", "config", "core.hooksPath", ".githooks"])

    result = run(["git", "config", "--get", "core.hooksPath"])
    hooks_path = result.stdout.strip()

    if hooks_path != ".githooks":
        print("Error: Failed to configure Git hooks.", file=sys.stderr)
        return 1

    pre_commit = Path(".githooks") / "pre-commit"
    if os.name != "nt" and pre_commit.exists():
        pre_commit.chmod(pre_commit.stat().st_mode | 0o111)

    print("Git Hooks installed successfully.")
    print(f"Hooks Path : {hooks_path}")

    if not has_command("clang-format"):
        print()
        print("Warning: clang-format was not found in PATH.")
        print("Please install LLVM/Clang before committing.")
    else:
        version = run(["clang-format", "--version"]).stdout.strip()
        print()
        print("Detected:")
        print(f"    {version}")

    print()
    print("Done.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())