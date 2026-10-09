#pragma once

#include "AMC/Lang/Lang.h"
#include "AMC/Core/Core.h"

#include <string>

// Rust language provider for AMC.
//
// `amc-rust` is a `amc-<language>` executable (see `AMC/Lang/Lang.h`).  It is
// deliberately self-contained: unlike `amc-cpp` it does not link Clang/LLVM.
// The frontend is a small, source-level extractor for the Rust constructs that
// cross an FFI boundary (`#[repr(C)]` structs/enums and `extern "C"`
// functions); the backend projects ABIX IR back into Rust using the
// ABI-normalised primitive identity so that a C ABI scalar becomes its
// `core::ffi::c_*` alias (`c_int`, `c_double`, ...).
namespace amc::rust {

// Rust source + `.abic.toml` -> ABIX IR.
int run_frontend(const char *config_path, const char *output_path);

// ABIX IR -> Rust projection (`repr(C)` types + metadata region).
int backend(const char *input, const char *output);

// Map a packed ABIX primitive descriptor (see `amc::primitive_abi`) to the
// Rust type that preserves the C ABI on the target. Exposed for tests.
std::string primitive_rust_type(uint32_t primitive_abi);

// Render the Rust projection for a module. Exposed for tests.
std::string generate_projection(const amc::AbiModule &module);

}   // namespace amc::rust
