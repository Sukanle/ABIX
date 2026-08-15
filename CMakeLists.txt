cmake_minimum_required(VERSION 3.20)
project(AbiCrossDllTests LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(SKL_ABIX_BUILD_DLL_VARIANTS "Build different DLL variants for different toolchains to simulate cross-compiler behavior" ON)

set(SKL_ABIX_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SKL_ABIX_INC_DIR ${SKL_ABIX_SRC_DIR})

if(MSVC)
  add_compile_options(/utf-8)
  add_compile_options(/permissive-)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-fno-keep-inline-dllexport)
  endif()
endif()

find_package(Catch2 3 QUIET)

include(CTest)
if(BUILD_TESTING)
  enable_testing()
endif()
if(Catch2_FOUND)
  include(Catch)
  set(SKL_ABIX_HAVE_TEST RunnerBuildsTests ON)
else()
  set(SKL_ABIX_HAVE_TEST RunnerBuildsTests OFF)
  message(STATUS "No matching Catch2 found, only build plugin DLLs")
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
    target_link_options(${name} PRIVATE -Wl,--no-undefined)
  endif()
  if(WIN32)
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
abi_add_dll(callback_dll)
abi_add_dll(reload_dll_a)
abi_add_dll(reload_dll_b)
abi_add_dll(edge_dll)
abi_add_dll(edge_stdcall_dll)
abi_add_dll(hotcache_dll)
abi_add_dll(closed_dll)
abi_add_dll(closed_dll_v2)

include_directories(${SKL_ABIX_INC_DIR})
include_directories(${SKL_ABIX_INC_DIR}/Reflection)

if(Catch2_FOUND)
  add_subdirectory(test)
endif()

find_package(benchmark QUIET)
if(benchmark_FOUND)
  add_subdirectory(bench)
  message(STATUS "Google Benchmark found, building benchmarks")
else()
  message(STATUS "Google Benchmark not found, skipping benchmarks")
endif()