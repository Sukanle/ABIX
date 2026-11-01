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
# The ABIX runtime remains header-only; AMC uses the optional ABIX artifact
# libraries (format/abi/metadata/tools) and keeps its own exception/RTTI policy.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AMC_BUILD_TESTS "Build AMC unit/e2e tests" ON)
option(AMC_INSTALL "Generate AMC install rules" ON)
option(AMC_ENABLE_LLDB_PLUGIN "Build the optional LLDB plugin" ON)

find_package(ABIX 0.2 CONFIG REQUIRED COMPONENTS format abi metadata tools)
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

# AMC helpers (hex/name rendering, JSON text helpers and the structured error
# envelope). The sources are part of AMC, while ABIX supplies the artifact
# parser and ABI verification targets.
add_library(amc-util STATIC ${AMC_SOURCE_DIR}/src/SKL/AMC/Util/Names.cpp
                            ${AMC_SOURCE_DIR}/src/SKL/AMC/Util/Json.cpp
                            ${AMC_SOURCE_DIR}/src/SKL/AMC/Util/Error.cpp)
target_include_directories(amc-util PUBLIC ${AMC_INCLUDE_DIR})
target_link_libraries(amc-util PUBLIC ABIX::format fmt::fmt)

add_library(amc-lang STATIC ${AMC_SOURCE_DIR}/src/SKL/AMC/Lang/Lang.cpp)
target_include_directories(amc-lang PUBLIC ${AMC_INCLUDE_DIR})
target_link_libraries(amc-lang PUBLIC amc-util fmt::fmt tomlplusplus::tomlplusplus)

add_library(amc-core INTERFACE)
target_link_libraries(amc-core INTERFACE ABIX::format ABIX::abi ABIX::metadata ABIX::tools amc-util fmt::fmt)

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
