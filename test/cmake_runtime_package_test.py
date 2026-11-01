#!/usr/bin/env python3
"""Install a runtime-only ABIX package and consume ABIX::runtime without fmt."""

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
    parser.add_argument("source_root", help="ABIX source root")
    parser.add_argument("--cxx", help="C++ compiler for the runtime consumer")
    parser.add_argument("--keep-artifacts", action="store_true")
    args = parser.parse_args()

    cmake = shutil.which("cmake")
    if not cmake:
        print("  [SKIP] cmake not found")
        return 0

    work = tempfile.mkdtemp(prefix="abix-runtime-package.")
    prefix = os.path.join(work, "prefix")
    build = os.path.join(work, "build")
    consumer = os.path.join(work, "consumer")
    os.makedirs(consumer)

    try:
        # Build and install only the headers, MICS and the CMake package. No
        # AMC, LLVM, Catch2, benchmark or static library is needed.
        configure = [
            cmake,
            "-S", os.path.abspath(args.source_root),
            "-B", build,
            "-DABIX_BUILD_RUNTIME_ONLY=ON",
            "-DBUILD_TESTING=OFF",
            "-DBUILD_BENCHMARKS=OFF",
            "-DCMAKE_BUILD_TYPE=Release",
        ]
        if args.cxx:
            configure.append(f"-DCMAKE_CXX_COMPILER={args.cxx}")
        run(configure)
        run([cmake, "--install", build, "--prefix", prefix])

        with open(os.path.join(consumer, "CMakeLists.txt"), "w", encoding="utf-8") as handle:
            handle.write(
                "cmake_minimum_required(VERSION 3.20)\n"
                "project(abix_runtime_consumer LANGUAGES CXX)\n"
                "find_package(ABIX 0.2 CONFIG REQUIRED)\n"
                "add_executable(consumer main.cpp)\n"
                "target_link_libraries(consumer PRIVATE ABIX::runtime)\n"
            )
        with open(os.path.join(consumer, "main.cpp"), "w", encoding="utf-8") as handle:
            handle.write(
                "#include <SKL/ABIX/ABIX.h>\n"
                "#include <stdio.h>\n"
                "int main() {\n"
                "    const auto id = skl::abix::model::canonical_type_id(\"prim:i32\");\n"
                "    printf(\"runtime package ok %llu\\n\", static_cast<unsigned long long>(id.lo));\n"
                "    return 0;\n"
                "}\n"
            )

        configure = [
            cmake,
            "-S", consumer,
            "-B", os.path.join(consumer, "build"),
            f"-DCMAKE_PREFIX_PATH={prefix}",
            "-DCMAKE_DISABLE_FIND_PACKAGE_fmt=TRUE",
            "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
            "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF",
        ]
        if args.cxx:
            configure.append(f"-DCMAKE_CXX_COMPILER={args.cxx}")
        run(configure)
        run([cmake, "--build", os.path.join(consumer, "build")])
        result = run([os.path.join(consumer, "build", "consumer")])
        if "runtime package ok" not in result.stdout:
            print("  [FAIL] runtime package consumer did not run")
            return 1
        print("  [PASS] runtime-only ABIX package configured, built and ran without fmt")

        # The same runtime target must be consumable from a parent project via
        # add_subdirectory, without installing or building AMC/tests/DLLs.
        subproject = os.path.join(work, "subproject")
        os.makedirs(subproject)
        with open(os.path.join(subproject, "CMakeLists.txt"), "w", encoding="utf-8") as handle:
            handle.write(
                "cmake_minimum_required(VERSION 3.20)\n"
                "project(abix_subproject_consumer LANGUAGES CXX)\n"
                "set(ABIX_BUILD_RUNTIME_ONLY ON CACHE BOOL \"\" FORCE)\n"
                f"add_subdirectory({os.path.abspath(args.source_root)} abix)\n"
                "add_executable(consumer main.cpp)\n"
                "target_link_libraries(consumer PRIVATE ABIX::runtime)\n"
            )
        with open(os.path.join(subproject, "main.cpp"), "w", encoding="utf-8") as handle:
            handle.write(
                "#include <SKL/ABIX/Model/ABI.h>\n"
                "int main() { return skl::abix::model::i32 == skl::abix::model::canonical_type_id(\"prim:i32\") ? 0 : 1; }\n"
            )
        configure = [
            cmake,
            "-S", subproject,
            "-B", os.path.join(subproject, "build"),
            "-DCMAKE_BUILD_TYPE=Release",
        ]
        if args.cxx:
            configure.append(f"-DCMAKE_CXX_COMPILER={args.cxx}")
        run(configure)
        run([cmake, "--build", os.path.join(subproject, "build")])
        run([os.path.join(subproject, "build", "consumer")])
        print("  [PASS] runtime-only add_subdirectory consumer built and ran")
        return 0
    finally:
        if args.keep_artifacts or os.environ.get("KEEP_ARTIFACTS", "") == "1":
            print(f"  [KEEP] retained artifacts: {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
