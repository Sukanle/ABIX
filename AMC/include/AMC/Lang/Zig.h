#pragma once

#include "AMC/Core/Core.h"
#include "AMC/Lang/Lang.h"   // IWYU pragma: keep

#include <string>

// Zig language provider for AMC.
//
// `amc-zig` is a minimal `amc-<language>` executable whose only capability is
// the backend: it consumes the same ABIX IR as every other provider and
// projects it to Zig (`extern struct` types, `extern fn` declarations and
// comptime layout assertions). It exists to validate that `.abix` is a reusable
// IR rather than a single-language binding tool -- there is no Zig-specific
// frontend yet, and no Zig-specific information is added to the model.
namespace amc::lang::zig {

// ABIX IR -> Zig projection (`extern` types + extern functions + metadata region).
int backend(const char *input, const char *output);

// Map a packed ABIX primitive descriptor (see `amc::primitive_abi`) to the Zig
// type that preserves the C ABI on the target. Exposed for tests.
std::string primitive_zig_type(uint32_t primitive_abi);

// Render the Zig projection for a module. Exposed for tests.
std::string generate_projection(const amc::AbiModule &module);

}   // namespace amc::lang::zig
