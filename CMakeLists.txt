cmake_minimum_required(VERSION 3.20)
project(AbiCrossDllTests LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTING "Build tests" ON)
option(BUILD_BENCHMARKS "Build benchmarks" ON)
option(SKL_ABIX_DEVELOPMENT "Enable development-only benchmarks (internal diagnostics)" OFF)
option(SKL_ABIX_BUILD_DLL_VARIANTS "Build different DLL variants for different toolchains to simulate cross-compiler behavior" ON)
option(AMC_BUILD "Build the AMC C++ frontend/backend tools" ON)

if(DEFINED SKL_ABIX_RCU_EPOCH_BATCH)
    # 变量被定义，将其值传递给宏定义
    add_definitions(-DSKL_ABIX_RCU_EPOCH_BATCH=${SKL_ABIX_RCU_EPOCH_BATCH})
    message(STATUS "Use user defined RCU epoch batch size: ${SKL_ABIX_RCU_EPOCH_BATCH}")
else()
    remove_definitions(-DSKL_ABIX_RCU_EPOCH_BATCH)
    message(STATUS "Use default RCU epoch batch size")
endif()

if(DEFINED SKL_ABIX_RCU_BATCH_PUBLISH)
    # 变量被定义，将其值传递给宏定义
    add_definitions(-DSKL_ABIX_RCU_BATCH_PUBLISH=${SKL_ABIX_RCU_BATCH_PUBLISH})
    message(STATUS "Use user defined RCU batch publish size: ${SKL_ABIX_RCU_BATCH_PUBLISH}")
else()
    remove_definitions(-DSKL_ABIX_RCU_BATCH_PUBLISH)
    message(STATUS "Use default RCU batch publish size")
endif()

if(DEFINED SKL_ABIX_RCU_CACHE_LINE_SIZE)
    # 变量被定义，将其值传递给宏定义
    add_definitions(-DSKL_ABIX_RCU_CACHE_LINE_SIZE=${SKL_ABIX_RCU_CACHE_LINE_SIZE})
    message(STATUS "Use user defined RCU cache line size: ${SKL_ABIX_RCU_CACHE_LINE_SIZE}")
else()
    remove_definitions(-DSKL_ABIX_RCU_CACHE_LINE_SIZE)
    message(STATUS "Use default RCU cache line size")
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
      set(SANITIZER "none" CACHE STRING
          "Choose sanitizer to enable (Debug build, not supported on Windows): asan, tsan or none")
      set_property(CACHE SANITIZER PROPERTY STRINGS "none" "asan" "tsan")
      if(SANITIZER STREQUAL "asan")
          add_compile_options(-fsanitize=address,undefined)
          add_link_options( -fsanitize=address,undefined)
      elseif(SANITIZER STREQUAL "tsan")
          add_compile_options(-fsanitize=thread,undefined)
          add_link_options( -fsanitize=thread,undefined)
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
  if(WIN32)
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
  set_target_properties(${name} PROPERTIES
    PREFIX ""          
    OUTPUT_NAME ${name}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins/${name}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins/${name}
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins/${name}
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
  if(NOT WIN32)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      target_link_options(${name} PRIVATE -Wl,--no-undefined)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND 
    (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))
      add_custom_command(TARGET ${name} POST_BUILD
        COMMAND dsymutil $<TARGET_FILE:${name}> -o ${CMAKE_BINARY_DIR}/bin/${name}.dSYM
        COMMENT "Generate dSYM file for ${name} on macOS")
    endif()
  else()
    add_custom_command(TARGET ${name} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:${name}> ${CMAKE_BINARY_DIR}/bin/${name}
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

include_directories(${SKL_ABIX_INC_DIR})
include_directories(${SKL_ABIX_INC_DIR}/mics)

if(ABIX_Catch2_FOUND)
  add_subdirectory(test)
endif()

if(ABIX_benchmark_FOUND)
  add_subdirectory(bench)
endif()

file(CREATE_LINK ${CMAKE_BINARY_DIR}/compile_commands.json ${CMAKE_BINARY_DIR}/../compile_commands.json SYMBOLIC)

if(AMC_BUILD)
  if(ABIX_LLVM_FOUND)
    # Detect Clang resource directory at build time (avoids runtime
    # path-inference that breaks for standalone executables).
    find_program(ABIX_CLANG_EXECUTABLE NAMES clang PATHS /opt/homebrew/opt/llvm/bin NO_DEFAULT_PATH)
    if(ABIX_CLANG_EXECUTABLE)
      execute_process(
        COMMAND ${ABIX_CLANG_EXECUTABLE} -print-resource-dir
        OUTPUT_VARIABLE ABIX_CLANG_RESOURCE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
      )
      message(STATUS "Clang resource dir: ${ABIX_CLANG_RESOURCE_DIR}")
    endif()
    if(NOT ABIX_CLANG_RESOURCE_DIR)
      message(WARNING "Could not detect Clang resource dir; AMC frontend may fail to find built-in headers")
    endif()

    add_library(amc-core STATIC amc/core/amc_core.cpp)
    target_include_directories(amc-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/amc/core)
    add_executable(amc-cpp amc/cpp/amc_cpp.cpp)
    target_include_directories(amc-cpp PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/amc/core
      ${LLVM_INCLUDE_DIRS}
      ${CLANG_INCLUDE_DIRS})
    target_compile_definitions(amc-cpp PRIVATE
      ABIX_CLANG_RESOURCE_DIR="${ABIX_CLANG_RESOURCE_DIR}")
    target_link_libraries(amc-cpp PRIVATE amc-core clang-cpp LLVM fmt::fmt)
    add_executable(amc amc/main.cpp)
    target_include_directories(amc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/amc)
    target_link_libraries(amc PRIVATE amc-core)
    add_executable(amc-dump amc/dump/amc_dump.cpp)
    target_include_directories(amc-dump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/amc/core)
    target_link_libraries(amc-dump PRIVATE amc-core)
    add_executable(amc-core-test amc/tests/core_test.cpp)
    target_link_libraries(amc-core-test PRIVATE amc-core)
    add_test(NAME amc-core-test COMMAND amc-core-test)
    add_executable(amc-e2e-test amc/tests/amc_e2e_test.cpp)
    target_link_libraries(amc-e2e-test PRIVATE amc-core)
    add_test(NAME amc-e2e-test COMMAND amc-e2e-test)
    add_test(NAME amc-integration-test COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/amc/tests/amc_integration_test.sh ${CMAKE_BINARY_DIR}/bin)
    add_test(NAME amc-cmake-integration-test COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/amc/tests/amc_cmake_integration_test.sh ${CMAKE_BINARY_DIR}/bin ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(amc amc-cpp amc-dump amc-core-test amc-e2e-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  else()
    message(WARNING "LLVM/Clang not found: AMC tools will not be built")
  endif()
endif()