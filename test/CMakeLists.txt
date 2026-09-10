set(TEST_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(TEST_INC_DIR ${SKL_ABIX_SRC_DIR})

function(abi_add_test name)
  add_executable(${name} ${TEST_SRC_DIR}/${name}.cpp)
  target_include_directories(${name} PRIVATE ${TEST_INC_DIR} ${TEST_INC_DIR}/mics)
  target_link_libraries(${name} PRIVATE Catch2::Catch2WithMain)
  set_target_properties(${name} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND 
    (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"))
    add_custom_command(TARGET ${name} POST_BUILD
      COMMAND dsymutil $<TARGET_FILE:${name}> -o ${CMAKE_BINARY_DIR}/bin/${name}.dSYM
      COMMENT "Generate dSYM file for ${name} on macOS")
  endif()
  catch_discover_tests(${name})
endfunction()

abi_add_test(basic_test)
abi_add_test(resource_test)
abi_add_test(callback_test)
abi_add_test(version_test)
abi_add_test(lookup_test)
abi_add_test(reload_test)
abi_add_test(edge_test)
abi_add_test(mics_test)
abi_add_test(closed_test)
abi_add_test(logging_test)
abi_add_test(rcu_config_test)
abi_add_test(concurrency_test)
abi_add_test(bootstrap_test)
abi_add_test(metadata_registry_test)
abi_add_test(hash_test)
abi_add_test(compatibility_test)
abi_add_test(map_test)

# --- Unity build: all tests in a single executable ---
add_executable(test_all ${TEST_SRC_DIR}/test_all.cpp)
target_include_directories(test_all PRIVATE ${TEST_INC_DIR} ${TEST_INC_DIR}/mics)
target_link_libraries(test_all PRIVATE Catch2::Catch2WithMain)
set_target_properties(test_all PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(plugin math_dll version_dll sigcheck_dll resource_dll callback_dll
               reload_dll_a reload_dll_b edge_dll edge_stdcall_dll hotcache_dll
               closed_dll closed_dll_v2)
  add_custom_command(TARGET test_all POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_BINARY_DIR}/plugins/${plugin} ${CMAKE_BINARY_DIR}/bin
    COMMENT "Stage plugin ${plugin} for test_all")
endforeach()
catch_discover_tests(test_all)
