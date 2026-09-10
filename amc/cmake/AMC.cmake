include_guard(GLOBAL)

# Adds AMC artifact generation to an existing CMake target.
#
# amc_add_abi(
#   TARGET my_library
#   CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/api.abic.toml
#   ABIX_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/amc/api.abix
#   CPP_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/api_abix.hpp
#   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/include/api.hpp
# )
#
# ABIX_OUTPUT must match the output path declared by [[export]] after the
# build directory passed to `amc build -B` is applied. CPP_OUTPUT is optional;
# when present, AMC generates a C++ metadata projection and makes it available
# to TARGET as a generated source and include directory.

function(amc_add_abi)
  set(options)
  set(one_value_args TARGET CONFIG ABIX_OUTPUT CPP_OUTPUT)
  set(multi_value_args DEPENDS)
  cmake_parse_arguments(AMC "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  foreach(required TARGET CONFIG ABIX_OUTPUT)
    if(NOT AMC_${required})
      message(FATAL_ERROR "amc_add_abi requires ${required}")
    endif()
  endforeach()
  if(NOT TARGET ${AMC_TARGET})
    message(FATAL_ERROR "amc_add_abi TARGET does not exist: ${AMC_TARGET}")
  endif()

  get_filename_component(_amc_config "${AMC_CONFIG}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  get_filename_component(_amc_abix "${AMC_ABIX_OUTPUT}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
  get_filename_component(_amc_build_dir "${_amc_abix}" DIRECTORY)
  get_filename_component(_amc_build_dir "${_amc_build_dir}" DIRECTORY)

  if(DEFINED AMC_EXECUTABLE)
    set(_amc_command "${AMC_EXECUTABLE}")
  elseif(TARGET amc)
    set(_amc_command "$<TARGET_FILE:amc>")
  else()
    find_program(_amc_command NAMES amc REQUIRED)
  endif()

  add_custom_command(
    OUTPUT "${_amc_abix}"
    COMMAND "${_amc_command}" build -c "${_amc_config}" -B "${_amc_build_dir}"
    DEPENDS "${_amc_config}" ${AMC_DEPENDS}
    COMMENT "AMC: building ${_amc_abix}"
    VERBATIM)

  set(_amc_outputs "${_amc_abix}")
  if(AMC_CPP_OUTPUT)
    get_filename_component(_amc_cpp "${AMC_CPP_OUTPUT}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    get_filename_component(_amc_cpp_dir "${_amc_cpp}" DIRECTORY)
    add_custom_command(
      OUTPUT "${_amc_cpp}"
      COMMAND "${_amc_command}" generate "${_amc_abix}" -l cpp -o "${_amc_cpp}"
      DEPENDS "${_amc_abix}"
      COMMENT "AMC: generating C++ projection ${_amc_cpp}"
      VERBATIM)
    list(APPEND _amc_outputs "${_amc_cpp}")
    set_source_files_properties("${_amc_cpp}" PROPERTIES GENERATED TRUE)
    target_sources(${AMC_TARGET} PRIVATE "${_amc_cpp}")
    target_include_directories(${AMC_TARGET} PRIVATE "${_amc_cpp_dir}")
  endif()

  set(_amc_target "${AMC_TARGET}_amc")
  if(TARGET "${_amc_target}")
    message(FATAL_ERROR "amc_add_abi may only be called once for target ${AMC_TARGET}")
  endif()
  add_custom_target("${_amc_target}" DEPENDS ${_amc_outputs})
  add_dependencies(${AMC_TARGET} "${_amc_target}")
  if(TARGET amc)
    add_dependencies("${_amc_target}" amc)
  endif()
  if(TARGET amc-cpp)
    add_dependencies("${_amc_target}" amc-cpp)
  endif()
endfunction()
