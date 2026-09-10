# AMC CMake Integration

Include `AMC.cmake` and attach an ABI configuration to an existing target:

```cmake
include(path/to/AMC.cmake)

add_library(foo STATIC foo.cpp)
amc_add_abi(
  TARGET foo
  CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/foo.abic.toml
  ABIX_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/amc/build/foo.abix
  CPP_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/foo_abix.hpp
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/include/foo.hpp)
```

`ABIX_OUTPUT` must match the relative `[[export]].output` from the `.abic.toml`
when evaluated below the build directory implied by the output path. `CPP_OUTPUT`
is optional. When supplied, the generated header is attached to `TARGET` and its
directory is added to the target's private include directories.

Set `AMC_EXECUTABLE` when AMC is not a target in the current project.
