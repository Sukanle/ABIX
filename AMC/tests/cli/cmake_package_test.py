#!/usr/bin/env python3
"""Install ABIX into a temporary prefix and consume it with find_package(ABIX)."""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile


def run(command, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, **kwargs)
    if result.returncode != 0:
        print("command failed:", " ".join(command), file=sys.stderr)
        if result.stdout:
            print(result.stdout, file=sys.stderr)
        if result.stderr:
            print(result.stderr, file=sys.stderr)
        raise SystemExit(result.returncode)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", help="Configured ABIX build directory")
    parser.add_argument("source_root", help="ABIX source root")
    parser.add_argument("--cxx", help="C++ compiler for the consumer")
    parser.add_argument(
        "--fmt-dir",
        default=os.environ.get("fmt_DIR", ""),
        help="fmt CMake package directory used by the ABIX build",
    )
    parser.add_argument("--keep-artifacts", action="store_true")
    args = parser.parse_args()

    cmake = shutil.which("cmake")
    if not cmake:
        print("  [SKIP] cmake not found")
        return 0

    work = tempfile.mkdtemp(prefix="abix-package.")
    prefix = os.path.join(work, "prefix")
    consumer = os.path.join(work, "consumer")
    os.makedirs(consumer)

    try:
        run([cmake, "--install", os.path.abspath(args.build_dir), "--prefix", prefix])

        with open(os.path.join(consumer, "CMakeLists.txt"), "w", encoding="utf-8") as handle:
            handle.write(
                "cmake_minimum_required(VERSION 3.20)\n"
                "project(abix_package_consumer LANGUAGES CXX)\n"
                "find_package(ABIX 0.2 CONFIG REQUIRED)\n"
                "add_executable(consumer main.cpp)\n"
                "target_link_libraries(consumer PRIVATE ABIX::ABIX)\n"
            )
        with open(os.path.join(consumer, "main.cpp"), "w", encoding="utf-8") as handle:
            handle.write(
                "#include <SKL/ABIX/ABIX.h>\n"
                "#include <stdio.h>\n"
                "int main() {\n"
                "    const auto id = skl::abix::model::canonical_type_id(\"prim:i32\");\n"
                "    printf(\"package ok %llu\\n\", static_cast<unsigned long long>(id.lo));\n"
                "    return 0;\n"
                "}\n"
            )

        configure = [cmake, "-S", consumer, "-B", os.path.join(consumer, "build"),
                     f"-DCMAKE_PREFIX_PATH={prefix}"]
        if args.cxx:
            configure.append(f"-DCMAKE_CXX_COMPILER={args.cxx}")
        if args.fmt_dir:
            configure.append(f"-Dfmt_DIR={args.fmt_dir}")
        run(configure)
        run([cmake, "--build", os.path.join(consumer, "build")])
        result = run([os.path.join(consumer, "build",
                                   "consumer.exe" if sys.platform == "win32" else "consumer")])
        if "package ok" not in result.stdout:
            print("  [FAIL] package consumer did not run")
            return 1
        print("  [PASS] find_package(ABIX) consumer configured, built and ran")
        print("  [INFO] ABIX package test passed")
        return 0
    finally:
        if args.keep_artifacts or os.environ.get("KEEP_ARTIFACTS", "") == "1":
            print(f"  [KEEP] retained artifacts: {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
