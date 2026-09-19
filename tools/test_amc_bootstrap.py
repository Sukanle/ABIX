#!/usr/bin/env python3
# Verify AMC's metadata bootstrap loop for its own core IR.
#
# This is not compiler-source self-hosting: amc-cpp still uses Clang/LLVM to
# extract C++ semantics. It proves the compiled AMC stage can describe its
# core model, generate a valid .abix v4 artifact and C++ projection, and use
# that projection through RuntimeRegistry::type_of<T>().
#
# Usage:
#   python tool/test_amc_bootstrap.py
#   BUILD_DIR=/tmp/abix-build python tool/test_amc_bootstrap.py
#   AMC_BIN_DIR=/tmp/abix-build/bin python tool/test_amc_bootstrap.py
#   KEEP_ARTIFACTS=1 python tool/test_amc_bootstrap.py

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent


def fail(msg: str) -> None:
    print(f"[FAIL] {msg}", file=sys.stderr)
    sys.exit(1)


def run(
    cmd: list[str],
    cwd: Path | None = None,
    capture_output: bool = False,
    log_command: bool = True,
) -> subprocess.CompletedProcess[str]:
    """Run a command, print it, and raise SystemExit on failure."""
    if log_command:
        print(f"  \033[90m{' '.join(cmd)}\033[0m")
    result = subprocess.run(cmd, cwd=cwd, capture_output=capture_output, text=True)
    if result.returncode != 0:
        msg = f"Command failed (exit code {result.returncode}): {' '.join(cmd)}"
        if capture_output and result.stderr:
            msg += f"\n{result.stderr.strip()}"
        raise SystemExit(msg)
    return result


def require_command(name: str) -> str:
    """Ensure a command is available on PATH; return its path."""
    exe = shutil.which(name)
    if exe is None:
        fail(f"required command not found: {name}")
    return exe


def main() -> None:
    build_dir = Path(os.environ.get("BUILD_DIR", "/tmp/abix-amc-bootstrap-build"))
    external_amc_bin_dir = os.environ.get("AMC_BIN_DIR", "")
    keep_artifacts = os.environ.get("KEEP_ARTIFACTS", "0") == "1"

    # Temp working directory with manual cleanup (matching bash trap semantics)
    work_dir = Path(tempfile.mkdtemp(
        prefix="amc-bootstrap.", dir=os.environ.get("TMPDIR", "/tmp")
    ))

    print("=== AMC Core Metadata Bootstrap ===")
    print(f"[INFO] source root: {PROJECT_ROOT}")
    print(f"[INFO] build dir:   {build_dir}")
    print(f"[INFO] work dir:    {work_dir}")

    require_command("cmake")
    require_command("clang++")

    if external_amc_bin_dir:
        amc_bin_dir = Path(external_amc_bin_dir)
        amc = amc_bin_dir / "amc"
        amc_cpp = amc_bin_dir / "amc-cpp"
        if not amc.is_file() or not amc_cpp.is_file():
            fail(f"AMC_BIN_DIR must contain executable amc and amc-cpp: {amc_bin_dir}")
        if not os.access(amc, os.X_OK) or not os.access(amc_cpp, os.X_OK):
            fail(f"AMC_BIN_DIR must contain executable amc and amc-cpp: {amc_bin_dir}")
    else:
        amc_bin_dir = build_dir / "bin"
        amc = amc_bin_dir / "amc"
        amc_cpp = amc_bin_dir / "amc-cpp"

        print("[STEP] configure and build AMC")
        cmake_cache = build_dir / "CMakeCache.txt"
        if not cmake_cache.is_file():
            run(["cmake", "-S", str(PROJECT_ROOT), "-B", str(build_dir),
                 "-DCMAKE_BUILD_TYPE=Release"])
        run(["cmake", "--build", str(build_dir), "--target", "amc", "amc-cpp", "-j2"])

    if not amc.is_file() or not os.access(amc, os.X_OK):
        fail(f"AMC executable was not produced: {amc}")

    artifact_build = work_dir / "build"
    artifact = artifact_build / "build" / "amc_core.abix"
    header = work_dir / "amc_core.hpp"
    consumer = work_dir / "amc_self_consumer"

    print("[STEP] extract AMC core metadata")
    amc_config = PROJECT_ROOT / "amc" / "self.abic.toml"
    run([str(amc), "build", "-c", str(amc_config), "-B", str(artifact_build)])

    if not artifact.is_file():
        fail(f"expected artifact was not produced: {artifact}")

    print("[STEP] validate and inspect artifact")
    run([str(amc), "validate", str(artifact)])
    inspect_result = run([str(amc), "inspect", str(artifact)], capture_output=True)
    inspect_output = inspect_result.stdout.strip()
    print(f"[INFO] {inspect_output}")

    if "package=amc_core" not in inspect_output:
        fail("artifact package is not amc_core")
    if "types=" not in inspect_output:
        fail("artifact contains no types")
    # Check that types != 0: look for "types=<N>" with N > 0
    types_match = re.search(r"types=(\d+)", inspect_output)
    if types_match is None or int(types_match.group(1)) == 0:
        fail("artifact contains no types")

    print("[STEP] generate C++ metadata projection")
    run([str(amc), "generate", str(artifact), "-l", "cpp", "-o", str(header)])

    if not header.is_file():
        fail(f"expected generated header was not produced: {header}")

    header_text = header.read_text()
    if "amc_AbiModule_ABIX" not in header_text:
        fail("projection lacks AbiModule descriptor")
    if "amc_MapOperation_ABIX" not in header_text:
        fail("projection lacks MapOperation descriptor")

    print("[STEP] compile and run native type_of<T>() consumer")
    consumer_src = PROJECT_ROOT / "amc" / "tests" / "amc_self_consumer.cpp"
    run([
        "clang++", "-std=c++17",
        f"-I{PROJECT_ROOT}", f"-I{work_dir}",
        str(consumer_src), "-o", str(consumer),
    ])
    run([str(consumer)])

    print("[PASS] AMC core metadata bootstrap succeeded")
    print("[PASS] verified build -> validate -> generate -> RuntimeRegistry::type_of<T>()")

    # Clean up (unless KEEP_ARTIFACTS is set)
    if keep_artifacts:
        print(f"[INFO] retained artifacts: {work_dir}")
    else:
        shutil.rmtree(work_dir, ignore_errors=True)


if __name__ == "__main__":
    main()