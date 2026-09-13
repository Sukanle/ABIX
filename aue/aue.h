#ifndef AUE_H
#define AUE_H

// Aue: Lua syntax + an ABIX boundary layer (plan direction 4).
//
// L0 Default and L1 Meta share one native module contract. L0 registers the
// module's C functions directly as Lua closures (the golden reference); L1
// registers an empty module table whose `__index` metamethod resolves each
// function through the ABIX contract at call time. The two layers differ only
// in dispatch, so their observable behaviour must be byte-identical — that is
// the semantic guarantee `abix-conformance` enforces.

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <cstddef>
#include <cstdint>

namespace aue {

// A Lua-callable native function: returns the number of results pushed.
using Function = int (*)(lua_State *state);

struct Entry {
    const char *name;    // module-qualified lookup key, e.g. "add"
    uint32_t arity;      // declared parameter count (validated by L1)
    Function function;   // native implementation
};

// The ABIX contract the boundary layer dispatches through. `abi_hash` is the
// `.abix` artifact hash both ends must agree on.
struct Contract {
    const char *module;
    const char *abi_hash;
    const Entry *entries;
    size_t entry_count;
};

// L0 Default: eagerly copy every function into the module table.
void register_direct(lua_State *state, const Contract &contract);

// L1 Meta: expose a lazy table; `__index` looks a function up in the contract
// and memoizes it on first access.
void register_meta(lua_State *state, const Contract &contract);

// --- the demo native module (`counter`) ----------------------------------
void register_counter_types(lua_State *state);
const Contract &counter_contract();

}   // namespace aue

#endif
