// M2 interop consumer: the SAME `.abix` artifact that drove the Rust consumer
// is projected to Zig and compiled here. Its only job is to prove the IR is
// reusable across languages -- no Zig-specific information was added to AMC.
//
// The projection (`gen_zig/amc_generated.zig`) declares the `extern struct`
// type, the `extern fn` entry points and comptime layout assertions. If the
// library and the metadata disagree, the comptime block fails the build.

const std = @import("std");
const abi = @import("gen_zig/amc_generated.zig");

pub fn main() void {
    const sum = abi.demo_add(2, 3);
    if (sum != 5) @panic("demo_add");

    var v = abi.DemoVec2{ .x = 4, .y = 2.5 };
    const scaled = abi.demo_scale(&v, 2.0);
    if (scaled != 13.0) @panic("demo_scale");

    var out: c_int = 0;
    const rc = abi.demo_div(10, 2, &out);
    if (rc != 0 or out != 5) @panic("demo_div");

    std.debug.print("zig interop ok: add={d} scale={d} out={d}\n", .{ sum, scaled, out });
}
