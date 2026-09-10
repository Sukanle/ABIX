cmake_minimum_required(VERSION 3.20)
project(AMCCMakeFixture LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
include("${AMC_MODULE_DIR}/AMC.cmake")

add_library(amc_cmake_consumer STATIC consumer.cpp)
amc_add_abi(
  TARGET amc_cmake_consumer
  CONFIG "${AMC_FIXTURE_DIR}/amc_test.abic.toml"
  ABIX_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/amc-output/build/amc_test.abix"
  CPP_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/amc_metadata.hpp"
  DEPENDS "${AMC_FIXTURE_DIR}/amc_test_types.hpp")
