cmake_minimum_required(VERSION 3.20)
project(AbiCrossDllTests LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTING "Build tests" ON)
option(BUILD_BENCHMARKS "Build benchmarks" ON)
option(SKL_ABIX_DEVELOPMENT "Enable development-only benchmarks (internal diagnostics)" OFF)
option(SKL_ABIX_RCU_ENABLE_STATS "Collect RCU advancement diagnostics (advance contention/timeout counters)" OFF)
option(SKL_ABIX_BUILD_DLL_VARIANTS
       "Build different DLL variants for different toolchains to simulate cross-compiler behavior" ON)
option(AMC_BUILD "Build the AMC C++ frontend/backend tools" ON)
option(SKL_ABIX_KEEP_TEST_ARTIFACTS
       "Keep the test suites' temporary artifacts on disk for inspection (sets KEEP_ARTIFACTS=1)" OFF)

if(DEFINED SKL_ABIX_RCU_EPOCH_BATCH)
    add_definitions(-DSKL_ABIX_RCU_EPOCH_BATCH=${SKL_ABIX_RCU_EPOCH_BATCH})
    message(STATUS "Use user defined RCU epoch batch size: ${SKL_ABIX_RCU_EPOCH_BATCH}")
else()
    remove_definitions(-DSKL_ABIX_RCU_EPOCH_BATCH)
    message(STATUS "Use default RCU epoch batch size")
endif()

if(DEFINED SKL_ABIX_RCU_BATCH_PUBLISH)
    add_definitions(-DSKL_ABIX_RCU_BATCH_PUBLISH=${SKL_ABIX_RCU_BATCH_PUBLISH})
    message(STATUS "Use user defined RCU batch publish size: ${SKL_ABIX_RCU_BATCH_PUBLISH}")
else()
    remove_definitions(-DSKL_ABIX_RCU_BATCH_PUBLISH)
    message(STATUS "Use default RCU batch publish size")
endif()

if(DEFINED SKL_ABIX_RCU_CACHE_LINE_SIZE)
    add_definitions(-DSKL_ABIX_RCU_CACHE_LINE_SIZE=${SKL_ABIX_RCU_CACHE_LINE_SIZE})
    message(STATUS "Use user defined RCU cache line size: ${SKL_ABIX_RCU_CACHE_LINE_SIZE}")
else()
    remove_definitions(-DSKL_ABIX_RCU_CACHE_LINE_SIZE)
    message(STATUS "Use default RCU cache line size")
endif()

if(SKL_ABIX_RCU_ENABLE_STATS)
    add_definitions(-DSKL_ABIX_RCU_ENABLE_STATS=1)
    message(STATUS "RCU advancement diagnostics enabled")
else()
    remove_definitions(-DSKL_ABIX_RCU_ENABLE_STATS)
    message(STATUS "RCU advancement diagnostics disabled (default)")
endif()

if(SKL_ABIX_DEVELOPMENT)
    add_definitions(-DSKL_ABIX_DEVELOPMENT)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_BUILD_TYPE Release)
else()
    set(CMAKE_BUILD_TYPE Debug)
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
    if(NOT WIN32)
        set(SANITIZER
            "none"
            CACHE STRING "Choose sanitizer to enable (Debug build, not supported on Windows): asan, tsan or none")
        set_property(CACHE SANITIZER PROPERTY STRINGS "none" "asan" "tsan")
        if(SANITIZER STREQUAL "asan")
            add_compile_options(-fsanitize=address,undefined)
            add_link_options(-fsanitize=address,undefined)
        elseif(SANITIZER STREQUAL "tsan")
            add_compile_options(-fsanitize=thread,undefined)
            add_link_options(-fsanitize=thread,undefined)
        elseif(SANITIZER STREQUAL "none")
            add_compile_options(-fno-sanitize=address,thread)
        else()
            message(FATAL_ERROR "Unknown sanitizer: ${SANITIZER}")
        endif()
    endif()
endif()

set(SKL_ABIX_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SKL_ABIX_INC_DIR ${SKL_ABIX_SRC_DIR})

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_options(/permissive-)
    add_definitions(-DCRT_SECURE_NO_WARNINGS -DCRT_NONSTDC_NO_WARNINGS)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
    # MinGW GCC needs this so inline functions exported with dllexport are still emitted. It is a GCC-only flag
    # (Clang rejects it), so restrict it to GNU on Windows.
    if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-fno-keep-inline-dllexport)
    endif()
endif()

# ---------------------------------------------------------------------------
# External dependencies (unified via cmake/AbixDependencies.cmake)
# ---------------------------------------------------------------------------
include(cmake/AbixDependencies.cmake)

include(CTest)
if(BUILD_TESTING)
    enable_testing()
endif()
if(ABIX_Catch2_FOUND)
    include(Catch)
    set(SKL_ABIX_HAVE_TEST RunnerBuildsTests ON)
else()
    set(SKL_ABIX_HAVE_TEST RunnerBuildsTests OFF)
    message(STATUS "Catch2 not found — only building plugin DLLs")
endif()

function(abi_add_dll name)
    add_library(${name} SHARED ${SKL_ABIX_SRC_DIR}/dlls/${name}.cpp)
    target_include_directories(${name} PRIVATE ${SKL_ABIX_INC_DIR})
    set_target_properties(
        ${name}
        PROPERTIES PREFIX ""
                   OUTPUT_NAME ${name}
                   RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins/${name}
                   LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins/${name}
                   ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins/${name}
                   CXX_VISIBILITY_PRESET hidden
                   VISIBILITY_INLINES_HIDDEN ON)
    if(NOT WIN32)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_link_options(${name} PRIVATE -Wl,--no-undefined)
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL
                                                                                             "RelWithDebInfo"))
            add_custom_command(
                TARGET ${name}
                POST_BUILD
                COMMAND dsymutil $<TARGET_FILE:${name}> -o ${CMAKE_BINARY_DIR}/bin/${name}.dSYM
                COMMENT "Generate dSYM file for ${name} on macOS")
        endif()
    else()
        add_custom_command(
            TARGET ${name}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${name}> ${CMAKE_BINARY_DIR}/bin/${name}
            COMMENT "Stage plugin ${name} to bin root")
    endif()
endfunction()

abi_add_dll(math_dll)
abi_add_dll(version_dll)
abi_add_dll(sigcheck_dll)
abi_add_dll(resource_dll)
if(WIN32)
    target_link_libraries(resource_dll PRIVATE ws2_32)
endif()
abi_add_dll(callback_dll)
abi_add_dll(reload_dll_a)
abi_add_dll(reload_dll_b)
abi_add_dll(edge_dll)
abi_add_dll(edge_stdcall_dll)
abi_add_dll(hotcache_dll)
abi_add_dll(closed_dll)
abi_add_dll(closed_dll_v2)
abi_add_dll(interface_dll)
abi_add_dll(owner_dll)

include_directories(${SKL_ABIX_INC_DIR})
include_directories(${SKL_ABIX_INC_DIR}/mics)

if(ABIX_Catch2_FOUND)
    add_subdirectory(test)
endif()

if(ABIX_benchmark_FOUND)
    add_subdirectory(bench)
endif()

# Expose compile_commands.json at the build root (the parent of the per-config build directory) for tooling such as
# clangd. A symlink is used where supported; on Windows (no SeCreateSymbolicLinkPrivilege by default) the file is
# refreshed as a copy at build time, because the compiler database is only produced during generation.
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    if(WIN32)
        add_custom_target(
            abix-compile-commands ALL
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/compile_commands.json
                    ${CMAKE_BINARY_DIR}/../compile_commands.json
            COMMENT "Refreshing compile_commands.json at the build root")
    else()
        file(CREATE_LINK ${CMAKE_BINARY_DIR}/compile_commands.json ${CMAKE_BINARY_DIR}/../compile_commands.json
             SYMBOLIC)
    endif()
endif()

if(AMC_BUILD)
    if(ABIX_LLVM_FOUND)
        # Detect Clang resource directory at build time (avoids runtime path-inference that breaks for standalone
        # executables).
        #
        # Use LLVM_TOOLS_BINARY_DIR (set by find_package(LLVM)) as the primary hint — this works across macOS (Homebrew
        # → /opt/homebrew/opt/llvm/bin), Linux (system LLVM → /usr/lib/llvm-*/bin), and other platforms without
        # hardcoding a single OS-specific path or using NO_DEFAULT_PATH.
        find_program(
            ABIX_CLANG_EXECUTABLE
            NAMES clang
            HINTS ${LLVM_TOOLS_BINARY_DIR})
        if(ABIX_CLANG_EXECUTABLE)
            execute_process(
                COMMAND ${ABIX_CLANG_EXECUTABLE} -print-resource-dir
                OUTPUT_VARIABLE ABIX_CLANG_RESOURCE_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE)
            message(STATUS "Clang resource dir: ${ABIX_CLANG_RESOURCE_DIR}")
        endif()
        if(NOT ABIX_CLANG_RESOURCE_DIR)
            message(WARNING "Could not detect Clang resource dir; AMC frontend may fail to find built-in headers")
        endif()

        if(NOT ABIX_tomlplusplus_FOUND)
            message(FATAL_ERROR "toml++ not found — required to build AMC (amc, amc-cpp).  "
                                "Install: brew install tomlplusplus")
        endif()

        # ------------------------------------------------------------------
        # libabix-* — the shared ABIX libraries (plan AI-PS).
        #
        # Concern-partitioned so LLDB / clangd / MCP / the runtime can link only what they need. Every library shares
        # libabix-format, so there is exactly one `.abix`/Metadata Region parser in the project.
        # ------------------------------------------------------------------
        add_library(
            libabix-format STATIC
            AMC/src/Core/Core.cpp # .abix v4 read/write + hashing + canonical form
            AMC/src/Core/Error.cpp
            AMC/src/Core/ELF.cpp
            AMC/src/Core/Json.cpp
            AMC/src/ljson/json.c
            AMC/src/ljson/jnum.c
            AMC/src/ljson/grisu2.c
            AMC/src/ljson/dragonbox.c)
        target_include_directories(libabix-format PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/AMC/include
                                                         ${CMAKE_CURRENT_SOURCE_DIR}/AMC/include/ljson)
        # Core.cpp / Error.cpp use fmt; every libabix-* library links this one, so
        # a PUBLIC link here also supplies the include directory to its dependents.
        target_link_libraries(libabix-format PUBLIC fmt::fmt)
        # The vendored ljson C sources use C23 digit separators (e.g. 8'192); keep the rest of the project on C++17 and
        # compile only these files as C23.
        set_target_properties(libabix-format PROPERTIES C_STANDARD 23 C_STANDARD_REQUIRED ON)
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

        add_library(libabix-abi STATIC AMC/src/Core/Verify.cpp)
        target_link_libraries(libabix-abi PUBLIC libabix-format)

        add_library(
            libabix-metadata STATIC AMC/src/Core/Metadata.cpp # Metadata Region serialize/parse/materialize
                                    AMC/src/Core/Materialize.cpp AMC/src/Core/Context.cpp AMC/src/Core/Query.cpp)
        target_link_libraries(libabix-metadata PUBLIC libabix-abi)

        add_library(libabix-tools STATIC AMC/src/Core/Adapter.cpp AMC/src/Core/Lua.cpp AMC/src/Core/Symbol.cpp
                                        AMC/src/Core/Cache.cpp)
        target_link_libraries(libabix-tools PUBLIC libabix-metadata)

        # The libabix-* objects are linked into the LLDB plugin, which is a shared object, so they must be position
        # independent.
        set_target_properties(libabix-format libabix-abi libabix-metadata libabix-tools
                              PROPERTIES POSITION_INDEPENDENT_CODE ON)

        # Header-only runtime registry/binding layer; consumers link it for the `ABIX/` include root.
        add_library(libabix-runtime INTERFACE)
        target_include_directories(libabix-runtime INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

        # Backwards-compatible aggregate used by the AMC executables and tests. Shared helpers for the AMC executables
        # (hex/name rendering, JSON text helpers, the unified error envelope).
        add_library(amc-util STATIC AMC/src/Util/Names.cpp AMC/src/Util/Json.cpp AMC/src/Util/Error.cpp)
        target_include_directories(amc-util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/AMC/include)
        target_link_libraries(amc-util PUBLIC libabix-format fmt::fmt)

        # Language-provider infrastructure shared by every `amc-<language>` executable: `.abic.toml` import config,
        # JSONL IPC and capability dispatch.
        add_library(amc-lang STATIC AMC/src/Lang/Lang.cpp)
        target_include_directories(amc-lang PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/AMC/include)
        target_link_libraries(amc-lang PUBLIC amc-util fmt::fmt tomlplusplus::tomlplusplus)

        add_library(amc-core INTERFACE)
        target_link_libraries(amc-core INTERFACE libabix-format libabix-abi libabix-metadata libabix-tools amc-util
                                                 fmt::fmt)

        add_executable(amc-cpp AMC/src/Lang/C++/C++.cpp AMC/src/Lang/C++/Frontend.cpp AMC/src/Lang/C++/Backend.cpp)
        target_include_directories(amc-cpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/AMC/include)
        # LLVM/Clang headers are reached as ordinary `-I` directories, so under
        # -Wall -Wextra their own diagnostics (chiefly -Wunused-parameter in the
        # clang/* headers) flood the build log.  Marking them SYSTEM keeps the
        # warnings scoped to ABIX code.
        target_include_directories(amc-cpp SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
        target_compile_definitions(amc-cpp PRIVATE ABIX_CLANG_RESOURCE_DIR="${ABIX_CLANG_RESOURCE_DIR}")
        target_link_libraries(amc-cpp PRIVATE amc-core amc-lang clang-cpp LLVM fmt::fmt tomlplusplus::tomlplusplus)

        # Rust provider: a self-contained frontend (source-level #[repr(C)] / extern "C" extractor) and backend (ABIX ->
        # Rust repr(C) projection). Unlike amc-cpp it does not link Clang/LLVM.
        add_executable(amc-rust AMC/src/Lang/Rust/Rust.cpp AMC/src/Lang/Rust/Frontend.cpp AMC/src/Lang/Rust/Backend.cpp)
        target_include_directories(amc-rust PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/AMC/include)
        target_link_libraries(amc-rust PRIVATE amc-core amc-lang fmt::fmt)

        # Zig provider: backend-only, a minimal consumer of the same ABIX IR. It
        # exists to validate that `.abix` is reusable across languages; there is
        # no Zig frontend and no Zig-specific model.
        add_executable(amc-zig AMC/src/Lang/Zig/Zig.cpp AMC/src/Lang/Zig/Backend.cpp)
        target_include_directories(amc-zig PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/AMC/include)
        target_link_libraries(amc-zig PRIVATE amc-core amc-lang fmt::fmt)

        add_executable(
            amc
            AMC/main.cpp
            AMC/src/CLI/CLI.cpp
            AMC/src/CLI/Context.cpp
            AMC/src/CLI/Metadata.cpp
            AMC/src/CLI/Adapter.cpp
            AMC/src/CLI/Query.cpp
            AMC/src/CLI/Symbols.cpp
            AMC/src/CLI/Verify.cpp
            AMC/src/CLI/Diff.cpp
            AMC/src/CLI/Generate.cpp
            AMC/src/CLI/Build.cpp
            AMC/src/CLI/Cache.cpp
            AMC/src/CLI/Providers.cpp)
        target_include_directories(amc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/AMC/include)
        target_compile_definitions(
            amc PRIVATE AMC_DEFAULT_PROVIDERS_FILE="${CMAKE_CURRENT_SOURCE_DIR}/AMC/providers.toml")
        target_link_libraries(amc PRIVATE amc-core tomlplusplus::tomlplusplus)
        # `amc --list-languages` / `--describe-language` read this manifest. Keep
        # the source-tree copy next to the built executable so CTest and manual
        # runs use the same file; an installed layout can place it beside `amc`.
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/AMC/providers.toml
                       ${CMAKE_BINARY_DIR}/bin/amc-providers.toml COPYONLY)
        add_executable(amc-dump AMC/src/Tool/Dump/Dump.cpp AMC/src/Tool/Dump/Json.cpp AMC/src/Tool/Dump/Text.cpp)
        target_include_directories(amc-dump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/AMC/include)
        target_link_libraries(amc-dump PRIVATE amc-core)
        add_executable(amc-mcp AMC/src/Tool/MCP/MCP.cpp AMC/src/Tool/MCP/State.cpp AMC/src/Tool/MCP/Tools.cpp
                               AMC/src/Tool/MCP/Server.cpp)
        target_include_directories(amc-mcp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/AMC/include)
        target_link_libraries(amc-mcp PRIVATE amc-core)

        # ABIX LLDB C++ plugin (AI-P2): a native `abix` command linked against the shared libabix-* libraries. Loaded
        # into a debugger with `plugin load`.
        find_path(ABIX_LLDB_INCLUDE_DIR NAMES lldb/API/LLDB.h)
        find_library(ABIX_LLDB_LIBRARY NAMES lldb)
        if(ABIX_LLDB_INCLUDE_DIR AND ABIX_LLDB_LIBRARY)
            add_library(abix-lldb MODULE tools/lldb_abix.cpp)
            target_include_directories(abix-lldb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/AMC/include)
            # LLDB headers ship with LLVM; treat them as system headers for the
            # same reason as the clang/* includes above.
            target_include_directories(abix-lldb SYSTEM PRIVATE ${ABIX_LLDB_INCLUDE_DIR})
            target_link_libraries(abix-lldb PRIVATE amc-core ${ABIX_LLDB_LIBRARY})
            set_target_properties(abix-lldb PROPERTIES OUTPUT_NAME abix_lldb LIBRARY_OUTPUT_DIRECTORY
                                                                             ${CMAKE_BINARY_DIR}/bin)
            message(STATUS "ABIX LLDB plugin: ${ABIX_LLDB_LIBRARY}")
        else()
            message(STATUS "LLDB not found: the native ABIX LLDB plugin will not be built")
        endif()
        add_executable(amc-core-test AMC/tests/unit/core_test.cpp)
        target_include_directories(amc-core-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/AMC/include)
        target_link_libraries(amc-core-test PRIVATE amc-core)
        add_test(NAME amc-core-test COMMAND amc-core-test)
        add_executable(amc-e2e-test AMC/tests/e2e/e2e_test.cpp)
        target_include_directories(amc-e2e-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/AMC/include)
        target_link_libraries(amc-e2e-test PRIVATE amc-core)
        add_test(NAME amc-e2e-test COMMAND amc-e2e-test)
        find_program(AMC_PYTHON_EXECUTABLE NAMES python3 python)
        if(AMC_PYTHON_EXECUTABLE)
            add_test(NAME amc-integration-test
                     COMMAND ${AMC_PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/AMC/tests/cli/integration_test.py
                             ${CMAKE_BINARY_DIR}/bin)
            add_test(NAME amc-feature-test
                     COMMAND ${AMC_PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/AMC/tests/cli/feature_test.py
                             ${CMAKE_BINARY_DIR}/bin ${CMAKE_CURRENT_SOURCE_DIR})
            add_test(
                NAME amc-cmake-integration-test
                COMMAND ${AMC_PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/AMC/tests/cli/cmake_integration_test.py
                        ${CMAKE_BINARY_DIR}/bin ${CMAKE_CURRENT_SOURCE_DIR})
            add_test(NAME amc-rust-test
                     COMMAND ${AMC_PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/AMC/tests/providers/rust/conformance_test.py
                             ${CMAKE_BINARY_DIR}/bin ${CMAKE_CURRENT_SOURCE_DIR})
            add_test(NAME amc-interop-test
                     COMMAND ${AMC_PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/AMC/tests/interop/interop_test.py
                             ${CMAKE_BINARY_DIR}/bin ${CMAKE_CURRENT_SOURCE_DIR})
            # Real-library demo over cmdly (C++). Skips when cmdly/fmt/rustc are
            # unavailable.
            add_test(NAME amc-cmdly-interop-test
                     COMMAND ${AMC_PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/AMC/tests/interop/cmdly_test.py
                             ${CMAKE_BINARY_DIR}/bin ${CMAKE_CURRENT_SOURCE_DIR})
            # P0 automation: expose a C++ class as capabilities with no
            # hand-written adapter (auto thunks + metadata + Rust bindings).
            add_test(NAME amc-expose-interop-test
                     COMMAND ${AMC_PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/AMC/tests/interop/expose_test.py
                             ${CMAKE_BINARY_DIR}/bin ${CMAKE_CURRENT_SOURCE_DIR})
        else()
            message(WARNING "python3 not found: AMC integration tests will not be registered")
        endif()
        set_target_properties(
            amc
            amc-cpp
            amc-rust
            amc-zig
            amc-dump
            amc-mcp
            amc-core-test
            amc-e2e-test
            PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
        # Install the driver, language providers and the manifest together: the
        # manifest is resolved next to `amc`, so a split install would make
        # `--list-languages` fail at run time.
        install(
            TARGETS amc amc-cpp amc-rust amc-zig amc-dump amc-mcp
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/AMC/providers.toml
                DESTINATION ${CMAKE_INSTALL_BINDIR}
                RENAME amc-providers.toml)
        # SKL_ABIX_KEEP_TEST_ARTIFACTS: run every suite with KEEP_ARTIFACTS=1 so
        # its temporary directory is retained and printed for inspection.
        if(SKL_ABIX_KEEP_TEST_ARTIFACTS AND BUILD_TESTING)
            set(_abix_keep_tests amc-core-test amc-e2e-test)
            if(AMC_PYTHON_EXECUTABLE)
                list(APPEND _abix_keep_tests
                     amc-integration-test amc-feature-test amc-cmake-integration-test amc-rust-test
                     amc-interop-test amc-cmdly-interop-test amc-expose-interop-test)
            endif()
            set_tests_properties(${_abix_keep_tests} PROPERTIES ENVIRONMENT "KEEP_ARTIFACTS=1")
            message(STATUS "Test artifacts are retained: each suite prints its working directory (KEEP_ARTIFACTS=1)")
        endif()
    else()
        message(WARNING "LLVM/Clang not found: AMC tools will not be built")
    endif()
endif()

# ---------------------------------------------------------------------------
# Aue: Lua L0/L1 differential-conformance prototype (plan direction 4). Optional: built only when a Lua development
# package is available.
# ---------------------------------------------------------------------------
if(NOT WIN32)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LUA QUIET lua5.4)
    endif()
    if(NOT LUA_FOUND)
        find_path(LUA_INCLUDE_DIR lua.h PATH_SUFFIXES lua5.4)
        find_library(LUA_LIBRARY NAMES lua5.4 lua)
        if(LUA_INCLUDE_DIR AND LUA_LIBRARY)
            set(LUA_FOUND TRUE)
            set(LUA_INCLUDE_DIRS ${LUA_INCLUDE_DIR})
            set(LUA_LIBRARIES ${LUA_LIBRARY})
        endif()
    endif()
    if(LUA_FOUND)
        message(STATUS "Lua found: building the Aue conformance prototype")
        add_subdirectory(Aue)
    else()
        message(STATUS "Lua not found: Aue conformance prototype will not be built")
    endif()
endif()
