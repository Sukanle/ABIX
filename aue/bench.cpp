// Boundary-overhead benchmark: L0 (direct C binding) vs L1 (__index contract
// dispatch). It reports nanoseconds per full call:
//
//   l0_call       — resolve a raw field and pcall it
//   l1_call_warm  — resolve the memoized field and pcall it (steady state)
//   l1_call_cold  — drop the memoized field first, so every call takes the
//                   __index contract lookup (worst case)
//
// Timing is inherently noisy and this runs as a smoke test that must simply
// succeed; the numbers are printed for comparison, not asserted.
#include "aue.h"

#include <chrono>
#include <cstdio>
#include <cstdint>

namespace {

using Clock = std::chrono::steady_clock;

volatile int64_t g_sink = 0;

void push_counter(lua_State *state) { lua_getfield(state, LUA_REGISTRYINDEX, "aue_bench_counter"); }

double call_nanoseconds(lua_State *state, bool meta, bool force_index, int iterations) {
    const auto start = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        lua_getglobal(state, "counter");
        if (meta && force_index) {
            // Invalidate the memoized binding so __index runs again.
            lua_pushstring(state, "get");
            lua_pushnil(state);
            lua_rawset(state, -3);
        }
        lua_getfield(state, -1, "get");
        push_counter(state);
        if (lua_pcall(state, 1, 1, 0) == LUA_OK) {
            g_sink += static_cast<int64_t>(lua_tointeger(state, -1));
            lua_pop(state, 2);
        } else {
            lua_pop(state, 2);
        }
    }
    const auto end = Clock::now();
    return std::chrono::duration<double, std::nano>(end - start).count() / iterations;
}

bool prepare(bool meta, lua_State **out) {
    lua_State *state = luaL_newstate();
    if (state == nullptr) return false;
    luaL_openlibs(state);
    aue::register_counter_types(state);
    if (meta)
        aue::register_meta(state, aue::counter_contract());
    else
        aue::register_direct(state, aue::counter_contract());
    lua_setglobal(state, "counter");

    // One shared Counter stored in the registry for the call path.
    lua_getglobal(state, "counter");
    lua_getfield(state, -1, "new");
    lua_pushinteger(state, 0);
    if (lua_pcall(state, 1, 1, 0) != LUA_OK) {
        lua_close(state);
        return false;
    }
    lua_setfield(state, LUA_REGISTRYINDEX, "aue_bench_counter");
    lua_pop(state, 1);
    *out = state;
    return true;
}

}   // namespace

int main() {
    constexpr int kIterations = 200'000;

    lua_State *l0 = nullptr, *l1 = nullptr;
    if (!prepare(false, &l0) || !prepare(true, &l1)) {
        std::fprintf(stderr, "abix-bench: could not initialise the Lua state\n");
        return 1;
    }

    const double l0_call = call_nanoseconds(l0, false, false, kIterations);
    const double l1_call_warm = call_nanoseconds(l1, true, false, kIterations);
    const double l1_call_cold = call_nanoseconds(l1, true, true, kIterations);

    lua_close(l0);
    lua_close(l1);

    std::printf(
        "abix-bench: l0_call=%.1fns l1_call_warm=%.1fns l1_call_cold=%.1fns\n", l0_call, l1_call_warm, l1_call_cold);
    return 0;
}
