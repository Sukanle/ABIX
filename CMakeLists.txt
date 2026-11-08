cmake_minimum_required(VERSION 3.20)
project(AbiCrossDllTests VERSION 0.3.3 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(ABIX_IS_TOP_LEVEL ON)
else()
    set(ABIX_IS_TOP_LEVEL OFF)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CheckCXXSourceCompiles)

# Detect the target's long-double ABI without running a target executable, so
# cross-compilation configures use the target compiler's <float.h> rather than
# the host's. The ABIX model maps LDBL_MANT_DIG to a canonical float format.
function(abix_probe_ldbl_mant_dig value out_var)
    set(cache_var "ABIX_LDBL_MANT_DIG_${value}")
    check_cxx_source_compiles(
        "
        #include <float.h>
        #if LDBL_MANT_DIG != ${value}
        #  error unexpected LDBL_MANT_DIG
        #endif
        int main() { return 0; }
        "
        ${cache_var})
    set(${out_var} "${${cache_var}}" PARENT_SCOPE)
endfunction()

set(ABIX_LDBL_MANT_DIG "unknown")
foreach(_abix_ldbl_value 53 64 106 113)
    abix_probe_ldbl_mant_dig(${_abix_ldbl_value} _abix_ldbl_match)
    if(_abix_ldbl_match)
        set(ABIX_LDBL_MANT_DIG "${_abix_ldbl_value}")
        break()
    endif()
endforeach()

if(ABIX_LDBL_MANT_DIG STREQUAL "53")
    set(ABIX_LONG_DOUBLE_FORMAT "IEEE binary64 (same as double)")
elseif(ABIX_LDBL_MANT_DIG STREQUAL "64")
    set(ABIX_LONG_DOUBLE_FORMAT "x87 80-bit extended")
elseif(ABIX_LDBL_MANT_DIG STREQUAL "106")
    set(ABIX_LONG_DOUBLE_FORMAT "PowerPC double-double")
elseif(ABIX_LDBL_MANT_DIG STREQUAL "113")
    set(ABIX_LONG_DOUBLE_FORMAT "IEEE binary128")
else()
    set(ABIX_LONG_DOUBLE_FORMAT "unknown")
endif()

message(STATUS "Target long double: ${ABIX_LONG_DOUBLE_FORMAT} (LDBL_MANT_DIG=${ABIX_LDBL_MANT_DIG})")

if(ABIX_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

if(ABIX_IS_TOP_LEVEL)
    set(_abix_default_tests ON)
    set(_abix_default_benchmarks ON)
    set(_abix_default_dll_variants ON)
    set(_abix_default_bundled_mics ON)
else()
    set(_abix_default_tests OFF)
    set(_abix_default_benchmarks OFF)
    set(_abix_default_dll_variants OFF)
    set(_abix_default_bundled_mics OFF)
endif()

option(BUILD_TESTING "Build tests" ${_abix_default_tests})
option(BUILD_BENCHMARKS "Build benchmarks" ${_abix_default_benchmarks})
option(SKL_ABIX_DEVELOPMENT "Enable development-only benchmarks (internal diagnostics)" OFF)
option(SKL_ABIX_RCU_ENABLE_STATS "Collect RCU advancement diagnostics (advance contention/timeout counters)" OFF)
option(
    SKL_ABIX_BUILD_DLL_VARIANTS
    "Build different DLL variants for different toolchains to simulate cross-compiler behavior"
    ${_abix_default_dll_variants})
option(ABIX_BUILD_RUNTIME_ONLY
       "Build and install only the header-only ABIX runtime; skip AMC, tests, benchmarks and DLLs" OFF)
option(ABIX_INSTALL "Generate ABIX install rules" ${ABIX_IS_TOP_LEVEL})
option(ABIX_RUNTIME_NO_EXCEPTIONS "Build ABIX runtime targets without C++ exceptions" ON)
option(ABIX_RUNTIME_NO_RTTI "Build ABIX runtime targets without RTTI" ON)
option(ABIX_ENABLE_QUILL_TEST "Build the optional Quill logging integration test" OFF)
set(ABIX_QUILL_ROOT "" CACHE PATH "Quill source root for the optional logging integration test")
option(ABIX_INSTALL_BUNDLED_MICS "Install the bundled MICS package with ABIX" ${_abix_default_bundled_mics})
option(SKL_ABIX_KEEP_TEST_ARTIFACTS
       "Keep the test suites' temporary artifacts on disk for inspection (sets KEEP_ARTIFACTS=1)" OFF)

if(ABIX_BUILD_RUNTIME_ONLY)
    set(BUILD_TESTING OFF)
    set(BUILD_BENCHMARKS OFF)
    set(SKL_ABIX_BUILD_DLL_VARIANTS OFF)
endif()

unset(_abix_default_tests)
unset(_abix_default_benchmarks)
unset(_abix_default_dll_variants)
unset(_abix_default_bundled_mics)

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

set(SKL_ABIX_PROJ_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SKL_ABIX_INC_DIR ${SKL_ABIX_PROJ_DIR}/include)

# ABIX's runtime is an explicit-control-flow layer: no exception landing pads
# and no RTTI metadata. Apply this only to ABIX runtime tests/plugins; AMC keeps
# the compiler defaults because it integrates LLVM/Clang/fmt/toml++ and uses
# try/catch.
function(abix_apply_runtime_contract target)
    if(ABIX_RUNTIME_NO_EXCEPTIONS)
        if(MSVC)
            # /EHs-c- disables C++ exception handling; keep the STL in the same
            # mode so its headers do not expect exception support.
            target_compile_options(${target} PRIVATE /EHs-c-)
            target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0)
        else()
            target_compile_options(${target} PRIVATE -fno-exceptions)
        endif()
    endif()
    if(ABIX_RUNTIME_NO_RTTI)
        if(MSVC)
            target_compile_options(${target} PRIVATE /GR-)
        else()
            target_compile_options(${target} PRIVATE -fno-rtti)
        endif()
    endif()
endfunction()

if(ABIX_IS_TOP_LEVEL)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set(CMAKE_BUILD_TYPE Release)
    else()
        set(CMAKE_BUILD_TYPE Debug)
        set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
        if(NOT WIN32)
            set(SANITIZER "none"
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
endif()

# ---------------------------------------------------------------------------
# External dependencies (unified via cmake/AbixDependencies.cmake)
# ---------------------------------------------------------------------------
include(cmake/AbixDependencies.cmake)

# ---------------------------------------------------------------------------
# MICS (header-only submodule)
# ---------------------------------------------------------------------------
# Import MICS's own CMake target instead of hardcoding its include directories
# in every subproject. The target also carries HLMD, which MICS.h includes
# directly.
if(NOT TARGET MICS::MICS)
    if(ABIX_INSTALL AND ABIX_INSTALL_BUNDLED_MICS AND NOT DEFINED MICS_INSTALL)
        # ABIXTargets references MICS::MICS, so an installable ABIX package must
        # carry the bundled MICS package too. Do not force a parent project's
        # MICS_INSTALL choice.
        set(MICS_INSTALL ON CACHE BOOL "Install MICS's CMake package")
    endif()
    add_subdirectory(${SKL_ABIX_PROJ_DIR}/MICS ${CMAKE_CURRENT_BINARY_DIR}/MICS)
endif()

include(CTest)
if(BUILD_TESTING)
    enable_testing()
endif()
if(BUILD_TESTING AND ABIX_Catch2_FOUND)
    include(Catch)
    set(SKL_ABIX_HAVE_TEST RunnerBuildsTests ON)
else()
    set(SKL_ABIX_HAVE_TEST RunnerBuildsTests OFF)
    message(STATUS "Tests disabled or Catch2 not found — skipping test runners")
endif()

# Header-only runtime target. ABIX runtime consumers only need the headers and
# MICS; AMC and its LLVM toolchain are a separate project/package.
add_library(libabix-runtime INTERFACE)
target_compile_features(libabix-runtime INTERFACE cxx_std_17)
target_include_directories(
    libabix-runtime INTERFACE $<BUILD_INTERFACE:${SKL_ABIX_INC_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(libabix-runtime INTERFACE MICS::MICS)
set_target_properties(libabix-runtime PROPERTIES EXPORT_NAME runtime)
add_library(ABIX::runtime ALIAS libabix-runtime)

# Aggregate target. It starts as runtime-only and receives the format/ABI/
# metadata/tools libraries below when those optional components are built.
add_library(abix INTERFACE)
target_link_libraries(abix INTERFACE libabix-runtime)
set_target_properties(abix PROPERTIES EXPORT_NAME ABIX)
add_library(ABIX::ABIX ALIAS abix)

function(abi_add_dll name)
    add_library(${name} SHARED ${SKL_ABIX_PROJ_DIR}/dlls/${name}.cpp)
    target_include_directories(${name} PRIVATE ${SKL_ABIX_INC_DIR})
    target_link_libraries(${name} PRIVATE MICS::MICS)
    set_target_properties(
        ${name}
        PROPERTIES
            PREFIX
            ""
            OUTPUT_NAME
            ${name}
            RUNTIME_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/plugins/${name}
            LIBRARY_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/plugins/${name}
            ARCHIVE_OUTPUT_DIRECTORY
            ${CMAKE_BINARY_DIR}/plugins/${name}
            CXX_VISIBILITY_PRESET
            hidden
            VISIBILITY_INLINES_HIDDEN
            ON)
    abix_apply_runtime_contract(${name})
    if(NOT WIN32)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_link_options(${name} PRIVATE -Wl,--no-undefined)
        elseif(
            CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND
            (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL
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

if(BUILD_TESTING)
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

    # `hotcache_dll.cpp` is generated (and gitignored), so a plain CMake
    # configure must be able to produce it without the tools/run_build.py
    # wrapper.
    set(SKL_ABIX_HOTCACHE_SOURCE ${SKL_ABIX_PROJ_DIR}/dlls/hotcache_dll.cpp)
    if(NOT EXISTS ${SKL_ABIX_HOTCACHE_SOURCE})
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(NOT Python3_Interpreter_FOUND)
            message(FATAL_ERROR "dlls/hotcache_dll.cpp is missing and Python 3 is required to generate it")
        endif()
        add_custom_command(
            OUTPUT ${SKL_ABIX_HOTCACHE_SOURCE}
            COMMAND ${Python3_EXECUTABLE} ${SKL_ABIX_PROJ_DIR}/tools/gen_hotcache_dll.py
            DEPENDS ${SKL_ABIX_PROJ_DIR}/tools/gen_hotcache_dll.py
            COMMENT "Generating dlls/hotcache_dll.cpp")
        set_source_files_properties(${SKL_ABIX_HOTCACHE_SOURCE} PROPERTIES GENERATED TRUE)
    endif()

    abi_add_dll(hotcache_dll)
    abi_add_dll(closed_dll)
    abi_add_dll(closed_dll_v2)
    abi_add_dll(interface_dll)
    abi_add_dll(owner_dll)
endif()

include_directories(${SKL_ABIX_PROJ_DIR})

if(BUILD_TESTING AND ABIX_Catch2_FOUND)
    add_subdirectory(test)
endif()

if(BUILD_BENCHMARKS AND ABIX_benchmark_FOUND)
    add_subdirectory(bench)
endif()

# Expose compile_commands.json at the build root (the parent of the per-config build directory) for tooling such as
# clangd. A symlink is used where supported; on Windows (no SeCreateSymbolicLinkPrivilege by default) the file is
# refreshed as a copy at build time, because the compiler database is only produced during generation.
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    if(WIN32)
        add_custom_target(
            abix-compile-commands
            ALL
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/compile_commands.json
                    ${CMAKE_BINARY_DIR}/../compile_commands.json
            COMMENT "Refreshing compile_commands.json at the build root")
    else()
        file(
            CREATE_LINK ${CMAKE_BINARY_DIR}/compile_commands.json ${CMAKE_BINARY_DIR}/../compile_commands.json SYMBOLIC)
    endif()
endif()

# ---------------------------------------------------------------------------
# ABIX CMake package: find_package(ABIX CONFIG REQUIRED)
#
# The runtime target, public headers, license and package metadata are always
# installed. The package is runtime-only; AMC owns its artifact libraries and
# is built as a separate project.
# ---------------------------------------------------------------------------
if(ABIX_INSTALL)
    set(ABIX_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/ABIX")

    install(
        TARGETS libabix-runtime abix
        EXPORT ABIXTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    # Keep the installed include tree in the same SKL/... layout as the source
    # tree, so both generated and handwritten headers resolve with one include root.
    install(DIRECTORY ${SKL_ABIX_INC_DIR}/SKL/ABIX DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/SKL)

    install(EXPORT ABIXTargets FILE ABIXTargets.cmake NAMESPACE ABIX:: DESTINATION ${ABIX_CMAKE_INSTALL_DIR})

    configure_package_config_file(
        ${SKL_ABIX_PROJ_DIR}/cmake/ABIXConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/ABIXConfig.cmake
        INSTALL_DESTINATION ${ABIX_CMAKE_INSTALL_DIR}
        PATH_VARS CMAKE_INSTALL_INCLUDEDIR)

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/ABIXConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

    install(
        FILES ${CMAKE_CURRENT_BINARY_DIR}/ABIXConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/ABIXConfigVersion.cmake
        DESTINATION ${ABIX_CMAKE_INSTALL_DIR})
    install(
        FILES ${SKL_ABIX_PROJ_DIR}/LICENSE ${SKL_ABIX_PROJ_DIR}/THIRD_PARTY_NOTICES.md
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/ABIX)
endif()

find_program(ABIX_PYTHON_EXECUTABLE NAMES python3 python)
if(BUILD_TESTING AND ABIX_PYTHON_EXECUTABLE)
    add_test(
        NAME abix-cmake-runtime-package-test
        COMMAND ${ABIX_PYTHON_EXECUTABLE} ${SKL_ABIX_PROJ_DIR}/test/cmake_runtime_package_test.py ${SKL_ABIX_PROJ_DIR}
                --cxx ${CMAKE_CXX_COMPILER})
    set_tests_properties(abix-cmake-runtime-package-test PROPERTIES TIMEOUT 120)
    if(SKL_ABIX_KEEP_TEST_ARTIFACTS)
        set_tests_properties(abix-cmake-runtime-package-test PROPERTIES ENVIRONMENT "KEEP_ARTIFACTS=1")
    endif()
endif()
