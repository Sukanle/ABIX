#pragma once

#include <stdint.h>

// A small, ordinary C++ library used by the cross-language example. It has no
// ABIX dependency: AMC describes this public C ABI and projects it to the
// consumer languages.
#if defined(_WIN32)
#  if defined(SHAPES_BUILD)
#    define SHAPES_API __declspec(dllexport)
#  else
#    define SHAPES_API __declspec(dllimport)
#  endif
#else
#  define SHAPES_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct ShapeVec {
    double x;
    double y;
};

SHAPES_API struct ShapeVec shape_add(struct ShapeVec a, struct ShapeVec b);

// Library-owned handle: the library allocates it and is the only side that
// frees it. The generated consumers release it through shape_destroy.
SHAPES_API struct ShapeVec *shape_create(double x, double y);
SHAPES_API int32_t shape_destroy(struct ShapeVec *shape);

// Error boundary: 0 on success, non-zero on failure; the result is written to
// the third parameter.
SHAPES_API int32_t shape_scale(struct ShapeVec *shape, double factor, struct ShapeVec *out);
SHAPES_API int32_t shape_div(double a, double b, double *out);

// Test aid that makes the ownership contract observable.
SHAPES_API int32_t shape_live_count(void);

#ifdef __cplusplus
}
#endif
