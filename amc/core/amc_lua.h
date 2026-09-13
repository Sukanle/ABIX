#ifndef AMC_LUA_H
#define AMC_LUA_H

#include "amc_core.h"

#include <string>

namespace amc {

// AI-P3: generates the Aue (Lua) boundary-layer contract from ABIX metadata.
//
// The emitted header defines an `aue::Contract` whose entries map each ABIX
// function to a native Lua C function symbol (`int (*)(lua_State*)`). L0 and
// L1 both consume this one contract, so both ends agree on the ABI hash and
// on the exact function set — the plan's "双端对齐 hash".
//
// Symbol naming: the ABIX function name sanitized to a C identifier
// (`Foo::bar` -> `Foo_bar`). The native module must export that symbol.
std::string generate_lua_contract(const AbiModule &module, std::string &error);

// The C identifier AMC uses for an ABIX function's native entry point.
std::string lua_function_symbol(std::string_view name);

// The Lua-visible key for a function: `<package>_` is stripped when present,
// so `counter_add` in package `counter` is exposed as `add`.
std::string lua_entry_name(std::string_view package, std::string_view name);

// Emits a Lua script that asserts the generated contract exposes every ABIX
// function and that the runtime contract's abi_hash matches the metadata.
// Run under both L0 and L1 it turns the generated contract into an executable
// conformance case (metadata -> generated Lua test -> L0/L1).
std::string generate_lua_conformance(const AbiModule &module, std::string &error);

}   // namespace amc

#endif
