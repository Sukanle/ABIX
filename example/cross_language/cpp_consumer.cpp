// C++ consumer for the cross-language example.
//
// The public C ABI comes from native/shapes.hpp. The generated projection adds
// compile-time layout assertions and ABIX metadata; including it here proves
// that the same artifact also describes the C++ side.
#include <assert.h>
#include <stdio.h>

#include "native/shapes.hpp"
#include "gen/amc_generated.hpp"

int main() {
    const ShapeVec sum = shape_add(ShapeVec{1.0, 2.0}, ShapeVec{3.0, 4.0});
    assert(sum.x == 4.0);
    assert(sum.y == 6.0);

    ShapeVec *owned = shape_create(3.0, 4.0);
    assert(owned != nullptr);
    assert(shape_live_count() == 1);
    assert(owned->x == 3.0);
    assert(shape_destroy(owned) == 0);
    assert(shape_live_count() == 0);

    ShapeVec input{2.0, 5.0};
    ShapeVec output{0.0, 0.0};
    assert(shape_scale(&input, 2.0, &output) == 0);
    assert(output.x == 4.0);
    assert(output.y == 10.0);
    assert(shape_scale(nullptr, 2.0, &output) != 0);

    double quotient = 0.0;
    assert(shape_div(9.0, 3.0, &quotient) == 0);
    assert(quotient == 3.0);
    assert(shape_div(1.0, 0.0, &quotient) != 0);

    printf("cpp example ok\n");
    return 0;
}
