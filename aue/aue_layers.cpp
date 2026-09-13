#include "aue.h"

#include <cstring>

namespace aue {
namespace {

// `__index(table, key)` for the L1 Meta layer. Upvalue 1 holds the contract.
int meta_index(lua_State *state) {
    const auto *contract = static_cast<const Contract *>(lua_touserdata(state, lua_upvalueindex(1)));
    const char *key = lua_tostring(state, 2);
    if (contract != nullptr && key != nullptr) {
        for (size_t i = 0; i < contract->entry_count; ++i) {
            if (std::strcmp(contract->entries[i].name, key) != 0) continue;
            lua_pushcfunction(state, contract->entries[i].function);
            // Memoize: table[key] = function, so the metamethod runs once.
            lua_pushvalue(state, 2);
            lua_pushvalue(state, -2);
            lua_rawset(state, 1);
            return 1;
        }
    }
    lua_pushnil(state);
    return 1;
}

void set_string_field(lua_State *state, const char *key, const char *value) {
    lua_pushstring(state, value);
    lua_setfield(state, -2, key);
}

}   // namespace

void register_direct(lua_State *state, const Contract &contract) {
    lua_newtable(state);
    for (size_t i = 0; i < contract.entry_count; ++i) {
        lua_pushcfunction(state, contract.entries[i].function);
        lua_setfield(state, -2, contract.entries[i].name);
    }
    set_string_field(state, "_module", contract.module);
    set_string_field(state, "_abi_hash", contract.abi_hash);
    set_string_field(state, "_layer", "l0");
}

void register_meta(lua_State *state, const Contract &contract) {
    lua_newtable(state);
    set_string_field(state, "_module", contract.module);
    set_string_field(state, "_abi_hash", contract.abi_hash);
    set_string_field(state, "_layer", "l1");

    lua_newtable(state);
    lua_pushlightuserdata(state, const_cast<Contract *>(&contract));
    lua_pushcclosure(state, meta_index, 1);
    lua_setfield(state, -2, "__index");
    lua_setmetatable(state, -2);
}

}   // namespace aue
