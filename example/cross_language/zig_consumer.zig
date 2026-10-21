// Zig consumer for the cross-language example.
//
// The same `.abix` that drives the Rust projection drives this Zig projection.
// The generated comptime block asserts size, alignment and field offsets before
// any native call is attempted.
const std = @import("std");
const abi = @import("gen/amc_generated.zig");

pub fn main() void {
    const sum = abi.shape_add(abi.ShapeVec{ .x = 1.0, .y = 2.0 }, abi.ShapeVec{ .x = 3.0, .y = 4.0 });
    if (sum.x != 4.0 or sum.y != 6.0) @panic("shape_add");

    const owned = abi.shape_create(3.0, 4.0);
    if (owned == null) @panic("shape_create");
    if (abi.shape_live_count() != 1) @panic("shape_live_count");
    if (abi.shape_destroy(owned) != 0) @panic("shape_destroy");
    if (abi.shape_live_count() != 0) @panic("shape_destroy live count");

    var input = abi.ShapeVec{ .x = 2.0, .y = 5.0 };
    var output = abi.ShapeVec{ .x = 0.0, .y = 0.0 };
    if (abi.shape_scale(&input, 2.0, &output) != 0) @panic("shape_scale status");
    if (output.x != 4.0 or output.y != 10.0) @panic("shape_scale result");

    var quotient: f64 = 0.0;
    if (abi.shape_div(9.0, 3.0, &quotient) != 0) @panic("shape_div status");
    if (quotient != 3.0) @panic("shape_div result");
    if (abi.shape_div(1.0, 0.0, &quotient) == 0) @panic("shape_div error");

    std.debug.print("zig example ok\n", .{});
}
