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

    // ABIX-STD borrowed span (M3): read the non-owning view the library hands out.
    const span = abi.demo_numbers();
    if (span.len != 5) @panic("span len");
    var total: i64 = 0;
    var i: usize = 0;
    while (i < span.len) : (i += 1) total += span.data[i];
    if (total != 15) @panic("span contents");
    if (abi.demo_sum(span) != 15) @panic("demo_sum");

    // ABIX-STD string view (P0): borrowed, read-only UTF-8 text.
    const greeting = abi.demo_greeting();
    if (greeting.len != 5) @panic("greeting len");
    if (greeting.encoding != 0) @panic("greeting encoding");
    if (abi.demo_str_len(greeting) != 5) @panic("demo_str_len");

    std.debug.print("zig interop ok: add={d} scale={d} out={d} sum={d}\n", .{ sum, scaled, out, total });
}
