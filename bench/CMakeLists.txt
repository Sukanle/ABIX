find_package(benchmark REQUIRED)

set(BENCH_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(BENCH_INC_DIR ${SKL_ABIX_SRC_DIR})

function(abi_add_bench name)
  add_executable(${name} ${BENCH_SRC_DIR}/${name}.cpp)
  target_include_directories(${name} PRIVATE ${BENCH_INC_DIR} ${BENCH_INC_DIR}/Reflection)
  target_link_libraries(${name} PRIVATE benchmark::benchmark benchmark::benchmark_main)
  set_target_properties(${name} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endfunction()

abi_add_bench(atomic_bench)
abi_add_bench(ebr_bench)
abi_add_bench(lookup_bench)
abi_add_bench(call_bench)
abi_add_bench(reload_bench)
abi_add_bench(stress_bench)

# --- Unity build: all benchmarks in a single executable ---
add_executable(bench_all ${BENCH_SRC_DIR}/bench_all.cpp)
target_include_directories(bench_all PRIVATE ${BENCH_INC_DIR} ${BENCH_INC_DIR}/Reflection)
target_link_libraries(bench_all PRIVATE benchmark::benchmark benchmark::benchmark_main)
set_target_properties(bench_all PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(plugin math_dll version_dll sigcheck_dll resource_dll callback_dll
               reload_dll_a reload_dll_b edge_dll edge_stdcall_dll hotcache_dll
               closed_dll closed_dll_v2)
  add_custom_command(TARGET bench_all POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_BINARY_DIR}/plugins/${plugin} ${CMAKE_BINARY_DIR}/bin
    COMMENT "Stage plugin ${plugin} for bench_all")
endforeach()