#include "shapes.hpp"

#include <new>

namespace {
int32_t g_live = 0;
}

extern "C" {

SHAPES_API struct ShapeVec shape_add(struct ShapeVec a, struct ShapeVec b) { return {a.x + b.x, a.y + b.y}; }

SHAPES_API struct ShapeVec *shape_create(double x, double y) {
    auto *shape = new (std::nothrow) ShapeVec{x, y};
    if (shape != nullptr) ++g_live;
    return shape;
}

SHAPES_API int32_t shape_destroy(struct ShapeVec *shape) {
    if (shape != nullptr) {
        delete shape;
        --g_live;
    }
    return 0;
}

SHAPES_API int32_t shape_scale(struct ShapeVec *shape, double factor, struct ShapeVec *out) {
    if (shape == nullptr || out == nullptr) return 2;
    out->x = shape->x * factor;
    out->y = shape->y * factor;
    return 0;
}

SHAPES_API int32_t shape_div(double a, double b, double *out) {
    if (out == nullptr) return 2;
    if (b == 0.0) return 1;
    *out = a / b;
    return 0;
}

SHAPES_API int32_t shape_live_count(void) { return g_live; }

}   // extern "C"
