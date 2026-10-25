# Standalone CMake project for the AMC self-description consumers.
#
# It exists so `tools/run_test.py --bootstrap` and anyone reproducing the
# bootstrap by hand drive the very same pipeline the library documents:
#
#   AMC/self.abic.toml --(amc build)--> build/amc_core.abix
#                     --(amc generate -l cpp)--> generated/amc_core.hpp
#                     --(C++17 compile)--> self_consumer
#
# amc_add_abi owns the first two steps and attaches both outputs to the
# target's incremental build graph, so touching an `.abic.toml` dependency
# re-runs the metadata pipeline instead of silently reusing a stale header.
#
# Configure:
#   cmake -S AMC/tests/consumers -B <build> -G Ninja \
#     -DAMC_MODULE_DIR=<root>/AMC/CMake \
#     -DABIX_SOURCE_ROOT=<root> \
#     -DAMC_EXECUTABLE=<dir>/amc

cmake_minimum_required(VERSION 3.20)
project(ABIXSelfConsumers LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT ABIX_SOURCE_ROOT)
  message(FATAL_ERROR "ABIX_SOURCE_ROOT must point at the ABIX source tree")
endif()

include("${AMC_MODULE_DIR}/AMC.cmake")

add_executable(self_consumer self_consumer.cpp)

# self_consumer.cpp reaches for `SKL/AMC/Core/Core.h`, which lives outside the
# include root amc_add_abi contributes (that root only serves `SKL/ABIX/...`).
target_include_directories(self_consumer PRIVATE "${ABIX_SOURCE_ROOT}/AMC/include")

amc_add_abi(
    TARGET
    self_consumer
    CONFIG
    "${ABIX_SOURCE_ROOT}/AMC/self.abic.toml"
    ABIX_OUTPUT
    "${CMAKE_CURRENT_BINARY_DIR}/build/amc_core.abix"
    CPP_OUTPUT
    "${CMAKE_CURRENT_BINARY_DIR}/generated/amc_core.hpp"
    DEPENDS
    "${ABIX_SOURCE_ROOT}/AMC/self_types.cpp"
    "${ABIX_SOURCE_ROOT}/AMC/include/SKL/AMC/Core/Core.h")
