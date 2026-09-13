// abix-conformance: run one Lua script under the L0 Default and L1 Meta
// boundary layers and require byte-identical observable behaviour.
//
// This turns "is the ABIX boundary layer semantically transparent?" into an
// automatically checked proposition (plan AI-P3): the same standard Lua runs
// unchanged; only the dispatch backend differs.
#include "aue.h"

#include <cstdio>
#include <string>

namespace {

std::string g_output;

bool rawget_is_function(lua_State *state, const char *key) {
    lua_getglobal(state, "counter");
    lua_pushstring(state, key);
    lua_rawget(state, -2);
    const bool result = lua_isfunction(state, -1) != 0;
    lua_pop(state, 2);
    return result;
}

bool check_dispatch(lua_State *state, bool meta, std::string &error) {
    if (!meta) {
        if (!rawget_is_function(state, "new")) {
            error = "L0 did not eagerly bind functions";
            return false;
        }
        return true;
    }
    if (rawget_is_function(state, "new")) {
        error = "L1 bound functions eagerly instead of using __index";
        return false;
    }
    lua_getglobal(state, "counter");
    lua_getfield(state, -1, "new");
    const bool resolved = lua_isfunction(state, -1) != 0;
    lua_pop(state, 2);
    if (!resolved) {
        error = "L1 __index did not resolve the function";
        return false;
    }
    if (!rawget_is_function(state, "new")) {
        error = "L1 did not memoize the resolved function";
        return false;
    }
    return true;
}

int capture_print(lua_State *state) {
    const int count = lua_gettop(state);
    for (int i = 1; i <= count; ++i) {
        if (i > 1) g_output += '\t';
        size_t length = 0;
        const char *text = luaL_tolstring(state, i, &length);
        g_output.append(text, length);
        lua_pop(state, 1);   // luaL_tolstring pushes the string
    }
    g_output += '\n';
    return 0;
}

bool run_layer(const std::string &script, bool meta, std::string &output, std::string &error) {
    lua_State *state = luaL_newstate();
    if (state == nullptr) {
        error = "cannot create Lua state";
        return false;
    }
    luaL_openlibs(state);
    aue::register_counter_types(state);

    lua_pushcfunction(state, capture_print);
    lua_setglobal(state, "print");

    if (meta)
        aue::register_meta(state, aue::counter_contract());
    else
        aue::register_direct(state, aue::counter_contract());
    lua_setglobal(state, "counter");

    // Prove the layers really differ in dispatch, not just in output:
    //  - L0 stores "new" eagerly, so rawget already sees a function.
    //  - L1 stores nothing; the first access goes through __index and is then
    //    memoized on the module table.
    if (!check_dispatch(state, meta, error)) {
        lua_close(state);
        return false;
    }

    g_output.clear();
    const int status = luaL_dofile(state, script.c_str());
    if (status != LUA_OK) {
        const char *message = lua_tostring(state, -1);
        error = message != nullptr ? message : "unknown Lua error";
        lua_close(state);
        return false;
    }
    output = g_output;
    lua_close(state);
    return true;
}

}   // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: abix-conformance <script.lua>\n");
        return 2;
    }
    std::string l0, l1, error;
    if (!run_layer(argv[1], false, l0, error)) {
        std::fprintf(stderr, "abix-conformance: L0 failed: %s\n", error.c_str());
        return 1;
    }
    if (!run_layer(argv[1], true, l1, error)) {
        std::fprintf(stderr, "abix-conformance: L1 failed: %s\n", error.c_str());
        return 1;
    }
    if (l0 != l1) {
        std::fprintf(stderr, "abix-conformance: L0 != L1\n--- L0 ---\n%s--- L1 ---\n%s", l0.c_str(), l1.c_str());
        return 1;
    }
    std::printf("abix-conformance: L0 == L1 (%zu bytes)\n%s", l0.size(), l0.c_str());
    return 0;
}
