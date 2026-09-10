find_package(benchmark REQUIRED)

set(BENCH_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(BENCH_INC_DIR ${SKL_ABIX_SRC_DIR} ${SKL_ABIX_SRC_DIR}/Reflection ${BENCH_SRC_DIR})

if(SKL_ABIX_DEVELOPMENT)
  add_compile_definitions(SKL_ABIX_DEVELOPMENT)
endif()

# file 可不填，默认是 ${name}.cpp
function(abi_add_bench)
  cmake_parse_arguments(BENCH "" "NAME" "SOURCES" ${ARGN})

  add_executable(${BENCH_NAME} ${BENCH_SOURCES})
  target_include_directories(${BENCH_NAME} PRIVATE ${BENCH_INC_DIR})
  target_link_libraries(${BENCH_NAME} PRIVATE benchmark::benchmark benchmark::benchmark_main)
  set_target_properties(${BENCH_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_custom_command(TARGET ${BENCH_NAME} POST_BUILD
      COMMAND dsymutil $<TARGET_FILE:${BENCH_NAME}> -o ${CMAKE_BINARY_DIR}/bin/${BENCH_NAME}.dSYM
      COMMENT "Generate dSYM file for ${BENCH_NAME} on macOS")
  endif()
endfunction()

abi_add_bench(NAME "atomic_bench" SOURCES "atomic_bench.cpp")
abi_add_bench(NAME "call_bench" SOURCES "call_bench.cpp")
abi_add_bench(NAME "reload_bench" SOURCES "reload_bench.cpp")
abi_add_bench(NAME "stress_bench" SOURCES "stress_bench.cpp")
abi_add_bench(NAME "falseSharing_bench" SOURCES "falseSharing_bench.cpp")
abi_add_bench(NAME "lookup_bench" SOURCES "lookup_bench.cpp")
abi_add_bench(NAME "abix_resolve_bench" SOURCES "abix/abix_resolve_bench.cpp")
abi_add_bench(NAME "abix_lookup_cross_bench" SOURCES "abix/abix_lookup_cross_bench.cpp")

# --- EBR benchmarks (unity build — convenience binary) ---
set(EBR_SOURCES "topology/topology.cpp" "workloads/workload.cpp" "workloads/workload_runner.cpp")
abi_add_bench(NAME "ebr_bench" SOURCES "ebr_bench.cpp" ${EBR_SOURCES})
abi_add_bench(NAME "ebr_profile_bench" SOURCES "ebr/profile_bench.cpp" ${EBR_SOURCES})

# --- Topology-aware EBR profile benchmark ---
# Uses CpuTopology detection + workload profile framework
# add_executable(ebr_profile_bench
#     ${BENCH_SRC_DIR}/ebr/profile_bench.cpp
#     ${BENCH_SRC_DIR}/topology/topology.cpp
#     ${BENCH_SRC_DIR}/workloads/workload.cpp
#     ${BENCH_SRC_DIR}/workloads/workload_runner.cpp)
# target_include_directories(ebr_profile_bench PRIVATE ${BENCH_INC_DIR})
# target_link_libraries(ebr_profile_bench PRIVATE benchmark::benchmark benchmark::benchmark_main)
# set_target_properties(ebr_profile_bench PROPERTIES
#     RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
#     add_custom_command(TARGET ebr_profile_bench POST_BUILD
#         COMMAND dsymutil $<TARGET_FILE:ebr_profile_bench> -o ${CMAKE_BINARY_DIR}/bin/ebr_profile_bench.dSYM
#         COMMENT "Generate dSYM file for ebr_profile_bench on macOS")
# endif()

# --- Unity build: all benchmarks in a single executable ---
add_executable(bench_all ${BENCH_SRC_DIR}/bench_all.cpp ${EBR_SOURCES})
target_include_directories(bench_all PRIVATE ${BENCH_INC_DIR})
target_link_libraries(bench_all PRIVATE benchmark::benchmark benchmark::benchmark_main)
set_target_properties(bench_all PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(plugin math_dll version_dll sigcheck_dll resource_dll callback_dll
  reload_dll_a reload_dll_b edge_dll edge_stdcall_dll hotcache_dll
  closed_dll closed_dll_v2)
  add_compile_definitions(${plugin} PRIVATE ALL_BENCHMARKS)
  add_custom_command(TARGET bench_all POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_BINARY_DIR}/plugins/${plugin} ${CMAKE_BINARY_DIR}/bin
    COMMENT "Stage plugin ${plugin} for bench_all")
endforeach()