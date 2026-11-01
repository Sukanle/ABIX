# AMC

AMC is the ABI Metadata Compiler/toolchain. This directory now has a standalone
CMake project so it can move toward an independently released repository.

## Standalone build

AMC consumes an installed ABIX package. It does not need the ABIX source tree.

```bash
cmake -S AMC -B build/AMC \
  -DABIX_DIR=/opt/abix/lib/cmake/ABIX \
  -DCMAKE_PREFIX_PATH="/opt/abix;/path/to/llvm;/path/to/fmt;/path/to/tomlplusplus" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/AMC --parallel
ctest --test-dir build/AMC --output-on-failure
cmake --install build/AMC --prefix /opt/amc
```

Required packages:

- `ABIX 0.3` runtime and MICS
- LLVM and Clang (`clang-cpp`)
- fmt
- toml++
- Catch2 v3 for the C++ tests (optional)

AMC owns the artifact-layer libraries (`format`, `abi`, `metadata`, `tools`) and
exports them as `AMC::format`, `AMC::abi`, `AMC::metadata` and `AMC::tools`. They
are compiled here rather than borrowed from ABIX, because their public headers
and namespace are AMC's `SKL/AMC/Core` API. ABIX runtime remains header-only;
AMC is the toolchain that needs LLVM/Clang and keeps its own exception/RTTI
policy.

The standalone CTest suite covers the C++ core/e2e tests, the installed package
consumer, feature tests, Rust provider/panic tests, cancellation, interop,
capability generation and an independent CMake integration test. The old
monorepo-only `amc-integration-test` and `amc-cmdly-interop-test` were retired
with the split.

## Installed package

```cmake
find_package(AMC 0.1 CONFIG REQUIRED)

add_library(my_contract STATIC contract.cpp)
amc_add_abi(
  TARGET my_contract
  CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/contract.abic.toml"
  ABIX_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/build/contract.abix"
  CPP_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/contract_metadata.hpp")
```

`AMC::amc`, the language providers, `AMC::amc-dump`, `AMC::amc-mcp` and the
artifact libraries `AMC::format` / `AMC::abi` / `AMC::metadata` / `AMC::tools`
are exported targets. `amc_add_abi` is installed with the package and resolves
the ABIX include directories through the imported ABIX targets.

This is the first migration step: the parent ABIX build still contains the
monorepo integration for the transition period, while this standalone project
proves that AMC can be built and packaged against an installed ABIX.
