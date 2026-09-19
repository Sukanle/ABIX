// The demo native module for the Aue conformance prototype.
//
// It is an ordinary C++ module with a Lua-visible contract: both the L0
// Default layer and the L1 Meta layer end up calling exactly these functions,
// so a differential test isolates the dispatch layer from the module itself.
//
// When AMC is available the build generates the contract from
// `counter.abic.toml` and feeds it in (AUE_HAVE_GENERATED_CONTRACT); otherwise
// a hand-written contract with the same entries is used.
#include "aue.h"

#ifdef AUE_HAVE_GENERATED_CONTRACT
#  include "counter_contract.hpp"
#endif

namespace {
struct Counter {
    int value;
};
}   // namespace

// External linkage so a generated contract can reference these symbols by name.
extern "C" int counter_new(lua_State *state) {
    auto *counter = static_cast<Counter *>(lua_newuserdatauv(state, sizeof(Counter), 0));
    counter->value = static_cast<int>(luaL_checkinteger(state, 1));
    luaL_setmetatable(state, "counter.Counter");
    return 1;
}

extern "C" int counter_add(lua_State *state) {
    auto *counter = static_cast<Counter *>(luaL_checkudata(state, 1, "counter.Counter"));
    counter->value += static_cast<int>(luaL_checkinteger(state, 2));
    return 0;
}

extern "C" int counter_get(lua_State *state) {
    const auto *counter = static_cast<const Counter *>(luaL_checkudata(state, 1, "counter.Counter"));
    lua_pushinteger(state, counter->value);
    return 1;
}

#ifndef AUE_HAVE_GENERATED_CONTRACT
namespace {
const aue::Entry kCounterEntries[] = {
    {"new", 1, counter_new},
    {"add", 2, counter_add},
    {"get", 1, counter_get},
};

const aue::Contract kCounterContract = {
    // Kept in sync with `aue/counter.abic.toml`; the generated conformance
    // script asserts the runtime contract and the metadata agree on this hash.
    "counter",
    "0x1cfd421d673a1d40eff2cac77295f156",
    kCounterEntries,
    3,
};
}   // namespace
#endif

namespace aue {

void register_counter_types(lua_State *state) {
    luaL_newmetatable(state, "counter.Counter");
    lua_pop(state, 1);
}

const Contract &counter_contract() {
#ifdef AUE_HAVE_GENERATED_CONTRACT
    return amc_generated::lua_contract;
#else
    return kCounterContract;
#endif
}

}   // namespace aue
