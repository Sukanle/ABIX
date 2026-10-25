// C++ interop consumer for the same native library used by the Rust and Zig
// consumers. It includes the public C ABI and the AMC-generated C++ projection,
// so the generated static assertions run before main().
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "demo_api.hpp"
#include "amc_generated.hpp"

int main() {
    assert(demo_add(2, 3) == 5);

    DemoVec2 value{4, 2.5};
    assert(demo_scale(&value, 2.0) == 13.0);

    DemoVec2 *owned = demo_vec2_create(7, 1.5);
    assert(owned != nullptr);
    assert(owned->x == 7);
    assert(demo_live_count() == 1);
    assert(demo_vec2_destroy(owned) == 0);
    assert(demo_live_count() == 0);

    int32_t out = 0;
    assert(demo_div(10, 2, &out) == 0);
    assert(out == 5);
    assert(demo_div(10, 0, &out) != 0);

    const abix_span_i32 numbers = demo_numbers();
    assert(numbers.len == 5);
    assert(numbers.data[0] == 1);
    assert(demo_sum(numbers) == 15);

    printf("cpp interop ok\n");
    return 0;
}
