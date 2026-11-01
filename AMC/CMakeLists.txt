cmake_minimum_required(VERSION 3.20)
project(AMC VERSION 0.1.0 LANGUAGES C CXX)

# Standalone AMC build. This file is the first step toward publishing AMC as an
# independent repository: it consumes an installed ABIX package and does not
# assume that the ABIX source tree is its parent.
#
#   cmake -S AMC -B build/AMC \
#     -DABIX_DIR=<prefix>/lib/cmake/ABIX \
#     -DCMAKE_PREFIX_PATH=<prefix>;/path/to/llvm
#
# The ABIX runtime remains header-only. AMC owns the artifact libraries
# (format/abi/metadata/tools) and keeps its own exception/RTTI policy.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AMC_BUILD_TESTS "Build AMC unit/e2e tests" ON)
option(AMC_INSTALL "Generate AMC install rules" ON)
option(AMC_ENABLE_LLDB_PLUGIN "Build the optional LLDB plugin" ON)

find_package(ABIX 0.2 CONFIG REQUIRED COMPONENTS runtime)
find_package(LLVM CONFIG REQUIRED)
find_package(Clang CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

set(AMC_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(AMC_INCLUDE_DIR ${AMC_SOURCE_DIR}/include)

find_program(AMC_CLANG_EXECUTABLE NAMES clang HINTS ${LLVM_TOOLS_BINARY_DIR})
if(AMC_CLANG_EXECUTABLE)
    execute_process(
        COMMAND ${AMC_CLANG_EXECUTABLE} -print-resource-dir
        OUTPUT_VARIABLE AMC_CLANG_RESOURCE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()
if(NOT AMC_CLANG_RESOURCE_DIR)
    message(WARNING "Could not detect the Clang resource directory; amc-cpp may not find built-in headers")
endif()

# AMC owns the artifact-layer libraries (`format`, `abi`, `metadata`, `tools`).
# They are compiled in this project; ABIX supplies only the runtime headers and
# MICS. This keeps the AMC release independent without splitting the AMC core
# headers/namespaces across two repositories.
add_library(
    libabix-format STATIC
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Core/Core.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Core/Error.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Core/ELF.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Core/Json.cpp
    ${AMC_SOURCE_DIR}/src/ljson/json.c
    ${AMC_SOURCE_DIR}/src/ljson/jnum.c
    ${AMC_SOURCE_DIR}/src/ljson/grisu2.c
    ${AMC_SOURCE_DIR}/src/ljson/dragonbox.c)
target_include_directories(
    libabix-format
    PUBLIC $<BUILD_INTERFACE:${AMC_INCLUDE_DIR}> $<BUILD_INTERFACE:${AMC_SOURCE_DIR}/include/ljson>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(libabix-format PUBLIC ABIX::runtime fmt::fmt MICS::MICS)
target_compile_features(libabix-format PUBLIC cxx_std_17)
set_target_properties(libabix-format PROPERTIES EXPORT_NAME format C_STANDARD 23 C_STANDARD_REQUIRED ON
                                               POSITION_INDEPENDENT_CODE ON OUTPUT_NAME abix-format)
target_compile_definitions(
    libabix-format
    PRIVATE JSON_PARSE_SKIP_COMMENT=1
            JSON_PARSE_LAST_COMMA=1
            JSON_PARSE_SPECIAL_CHAR=1
            JSON_PARSE_SPECIAL_QUOTES=0
            JSON_PARSE_HEX_NUM=0
            JSON_PARSE_SPECIAL_NUM=0
            JSON_PARSE_SPECIAL_DOUBLE=0
            JSON_PARSE_SINGLE_VALUE=1
            JSON_PARSE_FINISHED_CHAR=0
            JSON_ERROR_PRINT_ENABLE=0)

add_library(libabix-abi STATIC ${AMC_SOURCE_DIR}/src/SKL/AMC/Core/Verify.cpp)
target_link_libraries(libabix-abi PUBLIC libabix-format)
set_target_properties(libabix-abi PROPERTIES EXPORT_NAME abi POSITION_INDEPENDENT_CODE ON OUTPUT_NAME abix-abi)

add_library(
    libabix-metadata STATIC
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Core/Metadata.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Core/Materialize.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Core/Context.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Core/Query.cpp)
target_link_libraries(libabix-metadata PUBLIC libabix-abi)
set_target_properties(libabix-metadata PROPERTIES EXPORT_NAME metadata POSITION_INDEPENDENT_CODE ON
                                                    OUTPUT_NAME abix-metadata)

add_library(
    libabix-tools STATIC
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Core/Adapter.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Core/Lua.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Core/Symbol.cpp
    ${AMC_SOURCE_DIR}/src/SKL/AMC/Core/Cache.cpp)
target_link_libraries(libabix-tools PUBLIC libabix-metadata)
set_target_properties(libabix-tools PROPERTIES EXPORT_NAME tools POSITION_INDEPENDENT_CODE ON OUTPUT_NAME abix-tools)

add_library(AMC::format ALIAS libabix-format)
add_library(AMC::abi ALIAS libabix-abi)
add_library(AMC::metadata ALIAS libabix-metadata)
add_library(AMC::tools ALIAS libabix-tools)

# AMC helpers (hex/name rendering, JSON text helpers and the structured error
# envelope).
add_library(amc-util STATIC ${AMC_SOURCE_DIR}/src/SKL/AMC/Util/Names.cpp
                            ${AMC_SOURCE_DIR}/src/SKL/AMC/Util/Json.cpp
                            ${AMC_SOURCE_DIR}/src/SKL/AMC/Util/Error.cpp)
target_include_directories(amc-util PUBLIC ${AMC_INCLUDE_DIR})
target_link_libraries(amc-util PUBLIC libabix-format fmt::fmt)

add_library(amc-lang STATIC ${AMC_SOURCE_DIR}/src/SKL/AMC/Lang/Lang.cpp)
target_include_directories(amc-lang PUBLIC ${AMC_INCLUDE_DIR})
target_link_libraries(amc-lang PUBLIC amc-util fmt::fmt tomlplusplus::tomlplusplus)

add_library(amc-core INTERFACE)
target_link_libraries(amc-core INTERFACE libabix-format libabix-abi libabix-metadata libabix-tools amc-util fmt::fmt)

add_executable(amc-cpp ${AMC_SOURCE_DIR}/src/SKL/AMC/Lang/C++/C++.cpp
                       ${AMC_SOURCE_DIR}/src/SKL/AMC/Lang/C++/Frontend.cpp
                       ${AMC_SOURCE_DIR}/src/SKL/AMC/Lang/C++/Backend.cpp)
target_include_directories(amc-cpp PRIVATE ${AMC_INCLUDE_DIR})
target_include_directories(amc-cpp SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
target_compile_definitions(amc-cpp PRIVATE ABIX_CLANG_RESOURCE_DIR="${AMC_CLANG_RESOURCE_DIR}")
target_link_libraries(amc-cpp PRIVATE amc-core amc-lang clang-cpp LLVM fmt::fmt tomlplusplus::tomlplusplus)

add_executable(amc-rust ${AMC_SOURCE_DIR}/src/SKL/AMC/Lang/Rust/Rust.cpp
                        ${AMC_SOURCE_DIR}/src/SKL/AMC/Lang/Rust/Frontend.cpp
                        ${AMC_SOURCE_DIR}/src/SKL/AMC/Lang/Rust/Backend.cpp)
target_include_directories(amc-rust PRIVATE ${AMC_INCLUDE_DIR})
target_link_libraries(amc-rust PRIVATE amc-core amc-lang fmt::fmt)

add_executable(amc-zig ${AMC_SOURCE_DIR}/src/SKL/AMC/Lang/Zig/Zig.cpp
                       ${AMC_SOURCE_DIR}/src/SKL/AMC/Lang/Zig/Backend.cpp)
target_include_directories(amc-zig PRIVATE ${AMC_INCLUDE_DIR})
target_link_libraries(amc-zig PRIVATE amc-core amc-lang fmt::fmt)

add_executable(amc-cancel-test-provider ${AMC_SOURCE_DIR}/tests/providers/cancellation/cancellation_provider.cpp)
target_include_directories(amc-cancel-test-provider PRIVATE ${AMC_INCLUDE_DIR})
target_link_libraries(amc-cancel-test-provider PRIVATE amc-lang fmt::fmt)
set_target_properties(amc-cancel-test-provider PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(amc ${AMC_SOURCE_DIR}/main.cpp
                   ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/CLI.cpp
                   ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/Context.cpp
                   ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/Metadata.cpp
                   ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/Adapter.cpp
                   ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/Query.cpp
                   ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/Symbols.cpp
                   ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/Verify.cpp
                   ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/Diff.cpp
                   ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/Generate.cpp
                   ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/Build.cpp
                   ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/Cache.cpp
                   ${AMC_SOURCE_DIR}/src/SKL/AMC/CLI/Providers.cpp)
target_include_directories(amc PRIVATE ${AMC_INCLUDE_DIR})
target_compile_definitions(amc PRIVATE AMC_DEFAULT_PROVIDERS_FILE="${AMC_SOURCE_DIR}/providers.toml")
target_link_libraries(amc PRIVATE amc-core tomlplusplus::tomlplusplus)

add_executable(amc-dump ${AMC_SOURCE_DIR}/src/SKL/AMC/Tool/Dump/Dump.cpp
                        ${AMC_SOURCE_DIR}/src/SKL/AMC/Tool/Dump/Json.cpp
                        ${AMC_SOURCE_DIR}/src/SKL/AMC/Tool/Dump/Text.cpp)
target_include_directories(amc-dump PRIVATE ${AMC_INCLUDE_DIR})
target_link_libraries(amc-dump PRIVATE amc-core)

add_executable(amc-mcp ${AMC_SOURCE_DIR}/src/SKL/AMC/Tool/MCP/MCP.cpp
                       ${AMC_SOURCE_DIR}/src/SKL/AMC/Tool/MCP/State.cpp
                       ${AMC_SOURCE_DIR}/src/SKL/AMC/Tool/MCP/Tools.cpp
                       ${AMC_SOURCE_DIR}/src/SKL/AMC/Tool/MCP/Server.cpp)
target_include_directories(amc-mcp PRIVATE ${AMC_INCLUDE_DIR})
target_link_libraries(amc-mcp PRIVATE amc-core)

set_target_properties(amc amc-cpp amc-rust amc-zig amc-dump amc-mcp
                      PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
configure_file(${AMC_SOURCE_DIR}/providers.toml ${CMAKE_BINARY_DIR}/bin/amc-providers.toml COPYONLY)

if(AMC_ENABLE_LLDB_PLUGIN)
    find_path(AMC_LLDB_INCLUDE_DIR NAMES lldb/API/LLDB.h)
    find_library(AMC_LLDB_LIBRARY NAMES lldb)
    if(AMC_LLDB_INCLUDE_DIR AND AMC_LLDB_LIBRARY)
        add_library(amc-lldb MODULE ${AMC_SOURCE_DIR}/../tools/lldb_abix.cpp)
        target_include_directories(amc-lldb PRIVATE ${AMC_INCLUDE_DIR})
        target_include_directories(amc-lldb SYSTEM PRIVATE ${AMC_LLDB_INCLUDE_DIR})
        target_link_libraries(amc-lldb PRIVATE amc-core ${AMC_LLDB_LIBRARY})
        set_target_properties(amc-lldb PROPERTIES OUTPUT_NAME abix_lldb LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    else()
        message(STATUS "LLDB not found: the AMC LLDB plugin will not be built")
    endif()
endif()

if(AMC_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 QUIET)
    if(Catch2_FOUND)
        include(Catch)
        add_executable(amc-core-test ${AMC_SOURCE_DIR}/tests/unit/core_test.cpp)
        target_include_directories(amc-core-test PRIVATE ${AMC_INCLUDE_DIR})
        target_link_libraries(amc-core-test PRIVATE amc-core Catch2::Catch2WithMain)
        set_target_properties(amc-core-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
        add_test(NAME amc-core-test COMMAND amc-core-test)

        add_executable(amc-e2e-test ${AMC_SOURCE_DIR}/tests/e2e/e2e_test.cpp)
        target_include_directories(amc-e2e-test PRIVATE ${AMC_INCLUDE_DIR})
        target_link_libraries(amc-e2e-test PRIVATE amc-core Catch2::Catch2WithMain)
        set_target_properties(amc-e2e-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
        add_test(NAME amc-e2e-test COMMAND amc-e2e-test)
    else()
        message(STATUS "Catch2 v3 not found — AMC C++ tests will not be built")
    endif()
endif()

if(AMC_INSTALL)
    set(AMC_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/AMC")

    install(
        TARGETS amc amc-cpp amc-rust amc-zig amc-dump amc-mcp
        EXPORT AMCTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(
        TARGETS libabix-format libabix-abi libabix-metadata libabix-tools
        EXPORT AMCTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES ${AMC_SOURCE_DIR}/providers.toml DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME amc-providers.toml)
    install(DIRECTORY ${AMC_SOURCE_DIR}/include/SKL/AMC DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/SKL)
    install(DIRECTORY ${AMC_SOURCE_DIR}/include/ljson DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(
        EXPORT AMCTargets
        FILE AMCTargets.cmake
        NAMESPACE AMC::
        DESTINATION ${AMC_CMAKE_INSTALL_DIR})

    configure_package_config_file(
        ${AMC_SOURCE_DIR}/CMake/AMCConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/AMCConfig.cmake
        INSTALL_DESTINATION ${AMC_CMAKE_INSTALL_DIR}
        PATH_VARS CMAKE_INSTALL_INCLUDEDIR)

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/AMCConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMinorVersion)

    install(
        FILES ${CMAKE_CURRENT_BINARY_DIR}/AMCConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/AMCConfigVersion.cmake
              ${AMC_SOURCE_DIR}/CMake/AMC.cmake
        DESTINATION ${AMC_CMAKE_INSTALL_DIR})
endif()

find_program(AMC_PYTHON_EXECUTABLE NAMES python3 python)
if(AMC_BUILD_TESTS AND AMC_INSTALL AND AMC_PYTHON_EXECUTABLE)
    add_test(
        NAME amc-package-test
        COMMAND ${AMC_PYTHON_EXECUTABLE} ${AMC_SOURCE_DIR}/tests/cli/amc_package_test.py
                ${CMAKE_BINARY_DIR} ${AMC_SOURCE_DIR} --abix-dir=${ABIX_DIR} --fmt-dir=${fmt_DIR}
                --cxx=${CMAKE_CXX_COMPILER})
    set_tests_properties(amc-package-test PROPERTIES TIMEOUT 180)
    if(DEFINED ENV{KEEP_ARTIFACTS} AND "$ENV{KEEP_ARTIFACTS}" STREQUAL "1")
        set_tests_properties(amc-package-test PROPERTIES ENVIRONMENT "KEEP_ARTIFACTS=1")
    endif()
endif()

if(AMC_BUILD_TESTS AND AMC_PYTHON_EXECUTABLE)
    set(_amc_test_env "ABIX_INCLUDE_DIRS=${ABIX_INCLUDE_DIR}" "CXX=${CMAKE_CXX_COMPILER}")

    add_test(NAME amc-feature-test COMMAND ${AMC_PYTHON_EXECUTABLE} ${AMC_SOURCE_DIR}/tests/cli/feature_test.py
                                           ${CMAKE_BINARY_DIR}/bin ${AMC_SOURCE_DIR})
    add_test(NAME amc-rust-test COMMAND ${AMC_PYTHON_EXECUTABLE}
                                        ${AMC_SOURCE_DIR}/tests/providers/rust/conformance_test.py
                                        ${CMAKE_BINARY_DIR}/bin ${AMC_SOURCE_DIR})
    add_test(NAME amc-rust-panic-test COMMAND ${AMC_PYTHON_EXECUTABLE} ${AMC_SOURCE_DIR}/tests/interop/panic_test.py
                                              ${CMAKE_BINARY_DIR}/bin)
    add_test(NAME amc-cancellation-test COMMAND ${AMC_PYTHON_EXECUTABLE}
                                                ${AMC_SOURCE_DIR}/tests/providers/cancellation/cancellation_test.py
                                                ${CMAKE_BINARY_DIR}/bin)
    add_test(NAME amc-interop-test COMMAND ${AMC_PYTHON_EXECUTABLE} ${AMC_SOURCE_DIR}/tests/interop/interop_test.py
                                           ${CMAKE_BINARY_DIR}/bin ${AMC_SOURCE_DIR})
    add_test(NAME amc-expose-interop-test COMMAND ${AMC_PYTHON_EXECUTABLE}
                                                  ${AMC_SOURCE_DIR}/tests/interop/expose_test.py
                                                  ${CMAKE_BINARY_DIR}/bin ${AMC_SOURCE_DIR})

    set_tests_properties(amc-feature-test amc-rust-test amc-rust-panic-test amc-cancellation-test
                         amc-interop-test amc-expose-interop-test PROPERTIES ENVIRONMENT "${_amc_test_env}" TIMEOUT 180)
endif()
