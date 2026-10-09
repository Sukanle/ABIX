# Aue L0/L1 differential-conformance prototype.
#
# Built only when Lua is available (root CMake sets LUA_FOUND). L0 and L1 share
# the same native module; `abix-conformance` proves their observable behaviour
# is identical. When AMC is available the contract is generated from
# `counter.abic.toml` at build time, so the runtime binds to real ABIX metadata.

set(AUE_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(AUE_CONTRACT_HEADER "")

if(TARGET amc)
  # `amc build` execs the `amc-cpp` provider next to the driver, so the codegen
  # step must depend on that provider too — not only on the `amc` driver.
  # Without this a clean Ninja build runs the generator before amc-cpp exists
  # (provider exit 127).
  set(AUE_CONTRACT_DEPS $<TARGET_FILE:amc>)
  if(TARGET amc-cpp)
    list(APPEND AUE_CONTRACT_DEPS $<TARGET_FILE:amc-cpp>)
  endif()
  add_custom_command(
    OUTPUT ${AUE_GENERATED_DIR}/counter_contract.hpp
    COMMAND ${CMAKE_COMMAND} -E make_directory ${AUE_GENERATED_DIR}
    COMMAND $<TARGET_FILE:amc> build
            -c ${CMAKE_CURRENT_SOURCE_DIR}/counter.abic.toml
            -B ${AUE_GENERATED_DIR}/build
    COMMAND $<TARGET_FILE:amc> generate
            ${AUE_GENERATED_DIR}/build/build/counter.abix
            -l lua -o ${AUE_GENERATED_DIR}/counter_contract.hpp
    DEPENDS ${AUE_CONTRACT_DEPS}
            ${CMAKE_CURRENT_SOURCE_DIR}/counter.abic.toml
            ${CMAKE_CURRENT_SOURCE_DIR}/counter_api.hpp
    COMMENT "Generating the Aue contract from counter.abix"
    VERBATIM)
  add_custom_target(aue-contract DEPENDS ${AUE_GENERATED_DIR}/counter_contract.hpp)
  set_source_files_properties(${AUE_GENERATED_DIR}/counter_contract.hpp PROPERTIES GENERATED TRUE)
  set(AUE_CONTRACT_HEADER ${AUE_GENERATED_DIR}/counter_contract.hpp)
endif()

function(aue_configure_target target)
  if(LUA_INCLUDE_DIRS)
    target_include_directories(${target} PRIVATE ${LUA_INCLUDE_DIRS})
  endif()
  if(LUA_LIBRARY_DIRS)
    target_link_directories(${target} PRIVATE ${LUA_LIBRARY_DIRS})
  endif()
  target_link_libraries(${target} PRIVATE ${LUA_LIBRARIES})
  # The L1 Meta layer is compiled in; a runtime request for L2 degrades to L1.
  target_compile_definitions(${target} PRIVATE AUE_ENABLE_L1=1)
  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  if(AUE_CONTRACT_HEADER)
    target_sources(${target} PRIVATE ${AUE_CONTRACT_HEADER})
    target_include_directories(${target} PRIVATE ${AUE_GENERATED_DIR})
    target_compile_definitions(${target} PRIVATE AUE_HAVE_GENERATED_CONTRACT)
    add_dependencies(${target} aue-contract)
  endif()
endfunction()

if(NOT TARGET abix-conformance)
  add_executable(abix-conformance
    aue_layers.cpp
    counter.cpp
    conformance.cpp)
  aue_configure_target(abix-conformance)

  if(BUILD_TESTING)
    add_test(NAME aue-conformance
      COMMAND abix-conformance ${CMAKE_CURRENT_SOURCE_DIR}/conformance.lua)
    add_test(NAME aue-conformance-edges
      COMMAND abix-conformance ${CMAKE_CURRENT_SOURCE_DIR}/conformance_edges.lua)
    # Requesting L2 must degrade to the compiled-in L1 (downgrade only).
    add_test(NAME aue-level-downgrade
      COMMAND abix-conformance ${CMAKE_CURRENT_SOURCE_DIR}/conformance.lua --requested l2)
    set_tests_properties(aue-level-downgrade PROPERTIES
      PASS_REGULAR_EXPRESSION "requested=l2 selected=l1")
  endif()

  add_executable(abix-bench
    aue_layers.cpp
    counter.cpp
    bench.cpp)
  aue_configure_target(abix-bench)

  if(BUILD_TESTING)
    add_test(NAME aue-bench COMMAND abix-bench)
  endif()
endif()
