#pragma once

#include <stdint.h>

// Public C ABI of the M1 interop demo.
//
// Every function is exported with default visibility so a foreign-language
// consumer can bind to it directly, while AMC derives the same ABI facts from
// this header into `interop_demo.abix`. The two descriptions (the runtime
// symbols and the metadata artifact) must agree: the Rust consumer proves the
// generated projection matches the native layout at compile time and the call
// results at run time.

#if defined(_WIN32)
#  define DEMO_API __declspec(dllexport)
#else
#  define DEMO_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct DemoVec2 {
    int32_t x;
    double y;
};

// Pure value functions.
DEMO_API int32_t demo_add(int32_t a, int32_t b);
DEMO_API double demo_scale(const struct DemoVec2 *v, double factor);

// Library-owned handle: allocated by the library, released by the same library.
// This is the cross-DLL ownership contract in miniature -- the consumer never
// frees memory it did not allocate, it calls back into the owner.
DEMO_API struct DemoVec2 *demo_vec2_create(int32_t x, double y);
DEMO_API int32_t demo_vec2_destroy(struct DemoVec2 *v);

// Error boundary: 0 on success, a non-zero error code on failure.
DEMO_API int32_t demo_div(int32_t a, int32_t b, int32_t *out);

// Number of live handles currently owned by the library. A test aid that makes
// the destruction contract observable across the boundary.
DEMO_API int32_t demo_live_count();

#ifdef __cplusplus
}
#endif
