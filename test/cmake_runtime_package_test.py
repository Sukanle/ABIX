#!/usr/bin/env python3
"""Install a runtime-only ABIX package and consume ABIX::runtime without fmt."""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile


def run(command, **kwargs):
    result = subprocess.run(command, check=False, capture_output=True, text=True, **kwargs)
    if result.returncode != 0:
        print("command failed:", " ".join(command), file=sys.stderr)
        if result.stdout:
            print(result.stdout, file=sys.stderr)
        if result.stderr:
            print(result.stderr, file=sys.stderr)
        raise SystemExit(result.returncode)
    return result


def write_version_probe(consumer, requested_version):
    """Write a minimal consumer that only exercises the package version check."""
    os.makedirs(consumer, exist_ok=True)
    with open(os.path.join(consumer, "CMakeLists.txt"), "w", encoding="utf-8") as handle:
        handle.write(
            "cmake_minimum_required(VERSION 3.20)\n"
            "project(abix_version_probe LANGUAGES CXX)\n"
            f"find_package(ABIX {requested_version} CONFIG REQUIRED)\n"
        )


def expect_version_rejected(cmake, prefix, version, work, cxx):
    consumer = os.path.join(work, f"version-{version.replace('.', '-')}")
    write_version_probe(consumer, version)
    configure = [
        cmake,
        "-S",
        consumer,
        "-B",
        os.path.join(consumer, "build"),
        f"-DCMAKE_PREFIX_PATH={prefix}",
        "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF",
        "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF",
    ]
    if cxx:
        configure.append(f"-DCMAKE_CXX_COMPILER={cxx}")
    result = subprocess.run(configure, check=False, capture_output=True, text=True)
    if result.returncode == 0:
        print(f"  [FAIL] ABIX {version} request unexpectedly succeeded", file=sys.stderr)
        return False
    output = (result.stdout or "") + (result.stderr or "")
    if "ABIX" not in output and "version" not in output.lower():
        print(f"  [FAIL] ABIX {version} request failed for an unrelated reason", file=sys.stderr)
        print(output, file=sys.stderr)
        return False
    print(f"  [PASS] ABIX {version} request rejected by the package version file")
    return True


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
            "-S",
            os.path.abspath(args.source_root),
            "-B",
            build,
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
                "find_package(ABIX 0.4.0 CONFIG REQUIRED)\n"
                'if(NOT EXISTS "${ABIX_OWNER_HANDLE_JSON}")\n'
                '  message(FATAL_ERROR "missing ABIX owner handle metadata: ${ABIX_OWNER_HANDLE_JSON}")\n'
                "endif()\n"
                "add_executable(consumer main.cpp)\n"
                "target_link_libraries(consumer PRIVATE ABIX::runtime)\n"
            )
        with open(os.path.join(consumer, "main.cpp"), "w", encoding="utf-8") as handle:
            handle.write(
                "#include <SKL/ABIX/ABIX.h>\n"
                "#include <stdio.h>\n"
                "int main() {\n"
                '    const auto id = skl::abix::model::canonical_type_id("prim:i32");\n'
                '    printf("runtime package ok %llu\\n", static_cast<unsigned long long>(id.lo));\n'
                "    return 0;\n"
                "}\n"
            )

        configure = [
            cmake,
            "-S",
            consumer,
            "-B",
            os.path.join(consumer, "build"),
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

        # v0.4.0 is a breaking layout boundary. The package version file must
        # reject old 0.3.x requests and future 0.5.x requests, while accepting
        # the same 0.4.x line.
        if not expect_version_rejected(cmake, prefix, "0.3.0", work, args.cxx):
            return 1
        if not expect_version_rejected(cmake, prefix, "0.4.1", work, args.cxx):
            return 1

        # The same runtime target must be consumable from a parent project via
        # add_subdirectory, without installing or building AMC or its tests/DLLs.
        subproject = os.path.join(work, "subproject")
        os.makedirs(subproject)
        with open(os.path.join(subproject, "CMakeLists.txt"), "w", encoding="utf-8") as handle:
            handle.write(
                "cmake_minimum_required(VERSION 3.20)\n"
                "project(abix_subproject_consumer LANGUAGES CXX)\n"
                'set(ABIX_BUILD_RUNTIME_ONLY ON CACHE BOOL "" FORCE)\n'
                f"add_subdirectory({os.path.abspath(args.source_root)} abix)\n"
                "add_executable(consumer main.cpp)\n"
                "target_link_libraries(consumer PRIVATE ABIX::runtime)\n"
            )
        with open(os.path.join(subproject, "main.cpp"), "w", encoding="utf-8") as handle:
            handle.write(
                "#include <SKL/ABIX/Model/ABI.h>\n"
                'int main() { return skl::abix::model::i32 == skl::abix::model::canonical_type_id("prim:i32") ? 0 : 1; }\n'
            )
        configure = [
            cmake,
            "-S",
            subproject,
            "-B",
            os.path.join(subproject, "build"),
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
