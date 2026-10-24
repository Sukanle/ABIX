#include "SKL/ABIX/Interface/Declaration.h"
// #include "SKL/ABIX/Interface/Interface.h"

constexpr auto i32 = skl::abix::model::canonical_type_id("prim:i32");

#define list(X)               \
    X(area, int32_t, i32, {}) \
    X(move, int32_t, i32, {i32, i32})


SKL_ABIX_INTERFACE_TRAITS(Shape, list, SKL_ABIX_VERSION_NUMBER(0, 1, 0));
SKL_ABIX_INTERFACE_UNIQUE_DATA(Shape, list);
// SKL_ABIX_INTERFACE_DECLARE(UNIQUE, Shape, list, SKL_ABIX_VERSION_NUMBER(0, 1, 0));
