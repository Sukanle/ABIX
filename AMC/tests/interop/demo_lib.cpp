// Implementation of the M1 interop demo library.
//
// The library is an ordinary C++ shared object. Its only obligations toward
// the foreign consumer are the ones the ABI already states: exported symbols
// with a C calling convention, and the ownership contract in demo_api.hpp
// (this library allocates the handle and is the only one that frees it).
#include "demo_api.hpp"

#include <new>
#include <stdexcept>
#include <vector>

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

// ABIX-STD borrowed span (M3): the library keeps the backing array alive and
// hands out a non-owning view. The consumer must not release it.
DEMO_API abix_span_i32 demo_numbers() {
    static const int32_t kNumbers[] = {1, 2, 3, 4, 5};
    return abix_span_i32{kNumbers, 5};
}

DEMO_API int64_t demo_sum(abix_span_i32 values) {
    int64_t total = 0;
    for (uint64_t i = 0; i < values.len; ++i)
        total += values.data[i];
    return total;
}

DEMO_API abix_view_rev_i32 demo_numbers_rev() {
    static const int32_t kNumbers[] = {1, 2, 3, 4, 5};
    return abix_view_rev_i32{5, kNumbers};
}

DEMO_API abix_str_utf8 demo_greeting() {
    return abix_str_utf8{"hello", 5, static_cast<uint32_t>(abix_std::Encoding::utf8)};
}

DEMO_API int64_t demo_str_len(abix_str_utf8 text) { return static_cast<int64_t>(text.len); }

// A vector-like container: internal layout stays opaque; the view is built from
// the accessors below. The returned data pointer is borrowed -- valid until the
// buffer is mutated or destroyed.
struct DemoBuffer {
    std::vector<int32_t> values;
};

DEMO_API struct DemoBuffer *demo_buffer_create() { return new (std::nothrow) DemoBuffer{}; }

DEMO_API int32_t demo_buffer_push(struct DemoBuffer *buffer, int32_t value) {
    if (!buffer) return 1;
    buffer->values.push_back(value);
    return 0;
}

DEMO_API const int32_t *demo_buffer_data(const struct DemoBuffer *buffer) {
    return buffer ? buffer->values.data() : nullptr;
}

DEMO_API uint64_t demo_buffer_size(const struct DemoBuffer *buffer) {
    return buffer ? static_cast<uint64_t>(buffer->values.size()) : 0;
}

DEMO_API int32_t demo_buffer_destroy(struct DemoBuffer *buffer) {
    delete buffer;
    return 0;
}

}   // extern "C"