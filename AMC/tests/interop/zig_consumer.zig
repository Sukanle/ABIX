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

    // ABIX-STD P1 contracts: optional, result and callback.
    const opt = abi.demo_optional(1);
    if (opt.has_value != 1 or opt.value != 42) @panic("optional");
    if (abi.demo_optional(0).has_value != 0) @panic("optional absent");
    const res = abi.demo_result(5);
    if (res.status != 0 or res.value != 10) @panic("result");
    if (abi.demo_result(-1).status != 1) @panic("result error");
    const cb = abi.demo_callback();
    if (cb.invoke == null or cb.context == null) @panic("callback");

    // ABIX-STD P1 range: length + indexed access.
    const set = abi.demo_set_create();
    _ = abi.demo_set_insert(set, 3);
    _ = abi.demo_set_insert(set, 4);
    if (abi.demo_set_size(set) != 2) @panic("range len");
    if (abi.demo_set_at(set, 0) != 3) @panic("range at 0");
    _ = abi.demo_set_destroy(set);

    // P2: a Map via an opaque handle + accessors.
    const map = abi.demo_map_create();
    _ = abi.demo_map_put(map, 1, 10);
    if (abi.demo_map_size(map) != 1) @panic("map size");
    var out2: c_int = 0;
    _ = abi.demo_map_get(map, 1, &out2);
    if (out2 != 10) @panic("map get");
    _ = abi.demo_map_destroy(map);

    std.debug.print("zig interop ok: add={d} scale={d} out={d} sum={d}\n", .{ sum, scaled, out, total });
}
