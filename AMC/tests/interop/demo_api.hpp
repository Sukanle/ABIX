#pragma once

#include <stdint.h>

#include "../../../ABIX/STD/Span.h"       // abix_span_i32 — the ABIX-STD borrowed span
#include "../../../ABIX/STD/String.h"     // abix_str_utf8 — the ABIX-STD string view
#include "../../../ABIX/STD/Optional.h"   // abix_optional_i32 — the ABIX-STD optional
#include "../../../ABIX/STD/Result.h"     // abix_result_i32 — the ABIX-STD result
#include "../../../ABIX/STD/Callback.h"   // abix_callback — the ABIX-STD callback

// Public C ABI of the interop demo.
//
// Every function is exported with default visibility so a foreign-language
// consumer can bind to it directly, while AMC derives the same ABI facts from
// this header into `interop_demo.abix`. The two descriptions (the runtime
// symbols and the metadata artifact) must agree: the generated projections
// prove the layout at compile time and the call results at run time.
//
// It exercises the M1 contracts (ownership / error boundary / target) and the
// first M3 slice: an ABIX-STD borrowed span consumed by both Rust and Zig.

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

// Error boundary with an unknown status: the Rust projection must preserve the
// numeric code instead of collapsing it into a known enum variant.
DEMO_API int32_t demo_div_unknown(int32_t a, int32_t b, int32_t *out);

// Number of live handles currently owned by the library. A test aid that makes
// the destruction contract observable across the boundary.
DEMO_API int32_t demo_live_count();

// ABIX-STD borrowed span (M3): a non-owning view over library memory. The
// consumer must not release it; it is only valid while the library keeps the
// backing array alive (here, a static array).
DEMO_API abix_span_i32 demo_numbers();
DEMO_API int64_t demo_sum(abix_span_i32 values);

// A non-canonical sequence view: same two fields, different order. AMC lowers it
// to `abix_span_i32` with a generated adapter rather than treating it as the
// boundary entity directly.
DEMO_API abix_view_rev_i32 demo_numbers_rev();

// ABIX-STD string view (M3): a borrowed, read-only UTF-8 text view.
DEMO_API abix_str_utf8 demo_greeting();
DEMO_API int64_t demo_str_len(abix_str_utf8 text);

// A vector-like container (M3): its layout is opaque; it is lowered to a
// sequence view through the accessors below, so consumers never see the
// container's internals.
struct DemoBuffer;
DEMO_API struct DemoBuffer *demo_buffer_create();
DEMO_API int32_t demo_buffer_push(struct DemoBuffer *buffer, int32_t value);
DEMO_API const int32_t *demo_buffer_data(const struct DemoBuffer *buffer);
DEMO_API uint64_t demo_buffer_size(const struct DemoBuffer *buffer);
DEMO_API int32_t demo_buffer_destroy(struct DemoBuffer *buffer);

// ABIX-STD P1 contracts: optional, result and callback.
DEMO_API abix_optional_i32 demo_optional(int32_t present);
DEMO_API abix_result_i32 demo_result(int32_t value);
DEMO_API abix_callback demo_callback();
DEMO_API int32_t demo_callback_invoke(void *context);

// A non-contiguous container exposed as an iterable range (P1): length plus
// indexed access, without assuming contiguous memory.
struct DemoSet;
DEMO_API struct DemoSet *demo_set_create();
DEMO_API int32_t demo_set_insert(struct DemoSet *set, int32_t value);
DEMO_API uint64_t demo_set_size(const struct DemoSet *set);
DEMO_API int32_t demo_set_at(const struct DemoSet *set, uint64_t index);
DEMO_API int32_t demo_set_destroy(struct DemoSet *set);

// A Map exposed as an opaque handle (P2): no unified container type, only
// accessor operations over the handle.
struct DemoMap;
DEMO_API struct DemoMap *demo_map_create();
DEMO_API int32_t demo_map_put(struct DemoMap *map, int32_t key, int32_t value);
DEMO_API int32_t demo_map_get(const struct DemoMap *map, int32_t key, int32_t *out);
DEMO_API uint64_t demo_map_size(const struct DemoMap *map);
DEMO_API int32_t demo_map_destroy(struct DemoMap *map);

#ifdef __cplusplus
}
#endif
