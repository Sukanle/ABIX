// Implementation of the M1 interop demo library.
//
// The library is an ordinary C++ shared object. Its only obligations toward
// the foreign consumer are the ones the ABI already states: exported symbols
// with a C calling convention, and the ownership contract in demo_api.hpp
// (this library allocates the handle and is the only one that frees it).
#include "demo_api.hpp"

#include <new>
#include <stdexcept>

namespace {
// Live handles owned by the library, so the destruction contract is observable.
int32_t g_live = 0;
}   // namespace

extern "C" {

DEMO_API int32_t demo_add(int32_t a, int32_t b) { return a + b; }

DEMO_API double demo_scale(const struct DemoVec2 *v, double factor) {
    if (!v) return 0.0;
    return (static_cast<double>(v->x) + v->y) * factor;
}

DEMO_API struct DemoVec2 *demo_vec2_create(int32_t x, double y) {
    auto *v = new (std::nothrow) DemoVec2{x, y};
    if (v) ++g_live;
    return v;
}

DEMO_API int32_t demo_vec2_destroy(struct DemoVec2 *v) {
    if (v) {
        delete v;
        --g_live;
    }
    return 0;
}

DEMO_API int32_t demo_div(int32_t a, int32_t b, int32_t *out) {
    if (!out) return 2;
    // The native exception is caught here and converted to a status code, so it
    // never crosses the ABI boundary into the foreign caller.
    try {
        if (b == 0) throw std::runtime_error("division by zero");
        *out = a / b;
        return 0;
    } catch (const std::exception &) {
        return 1;
    }
}

DEMO_API int32_t demo_live_count() { return g_live; }

}   // extern "C"