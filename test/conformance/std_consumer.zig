// Zig consumer for the ABIX-STD v0.4.0 conformance fixture.
//
// Zig's `?T`, error unions and slices are not layout-equivalent to ABIX-STD.
// This consumer declares the explicit `extern struct` adapter and validates the
// layout at comptime before calling the native library.

const std = @import("std");

const AbixOptionalI32 = extern struct {
    discriminant: u32,
    reserved: u32,
    value: i32,
};

const AbixResultI32 = extern struct {
    status: i32,
    reserved: u32,
    value: i32,
};

const AbixStrUtf8 = extern struct {
    data: [*c]const u8,
    len: u64,
    encoding: u32,
    reserved: u32,
};

const AbixSpanI32 = extern struct {
    data: [*c]const i32,
    len: u64,
};

const AbixCallbackFn = *const fn (?*anyopaque) callconv(.c) i32;
const AbixCallback = extern struct {
    invoke: ?AbixCallbackFn,
    context: ?*anyopaque,
};

const AbixCallbackReleaseFn = *const fn (?*anyopaque) callconv(.c) void;
const AbixCallbackOwned = extern struct {
    invoke: ?AbixCallbackFn,
    context: ?*anyopaque,
    release: ?AbixCallbackReleaseFn,
    flags: u32,
    reserved: u32,
};

fn alignUp(value: usize, alignment: usize) usize {
    return (value + alignment - 1) / alignment * alignment;
}

fn max(a: usize, b: usize) usize {
    return if (a > b) a else b;
}

const pointer_size = @sizeOf(*const u8);
const string_len_offset = alignUp(pointer_size, @alignOf(u64));
const string_encoding_offset = alignUp(string_len_offset + @sizeOf(u64), @alignOf(u32));
const string_reserved_offset = string_encoding_offset + @sizeOf(u32);
const string_alignment = max(max(pointer_size, @alignOf(u64)), @alignOf(u32));
const string_size = alignUp(string_reserved_offset + @sizeOf(u32), string_alignment);

comptime {
    if (@sizeOf(AbixOptionalI32) != 12) @compileError("optional_i32 size");
    if (@alignOf(AbixOptionalI32) != 4) @compileError("optional_i32 align");
    if (@offsetOf(AbixOptionalI32, "discriminant") != 0) @compileError("optional_i32 discriminant");
    if (@offsetOf(AbixOptionalI32, "reserved") != 4) @compileError("optional_i32 reserved");
    if (@offsetOf(AbixOptionalI32, "value") != 8) @compileError("optional_i32 value");

    if (@sizeOf(AbixResultI32) != 12) @compileError("result_i32 size");
    if (@alignOf(AbixResultI32) != 4) @compileError("result_i32 align");
    if (@offsetOf(AbixResultI32, "status") != 0) @compileError("result_i32 status");
    if (@offsetOf(AbixResultI32, "reserved") != 4) @compileError("result_i32 reserved");
    if (@offsetOf(AbixResultI32, "value") != 8) @compileError("result_i32 value");

    if (@offsetOf(AbixStrUtf8, "data") != 0) @compileError("string data");
    if (@offsetOf(AbixStrUtf8, "len") != string_len_offset) @compileError("string len");
    if (@offsetOf(AbixStrUtf8, "encoding") != string_encoding_offset) @compileError("string encoding");
    if (@offsetOf(AbixStrUtf8, "reserved") != string_reserved_offset) @compileError("string reserved");
    if (@sizeOf(AbixStrUtf8) != string_size) @compileError("string size");
    if (@alignOf(AbixStrUtf8) != string_alignment) @compileError("string alignment");

    if (@sizeOf(AbixSpanI32) != 2 * pointer_size) @compileError("span size");
    if (@sizeOf(AbixCallback) != 2 * pointer_size) @compileError("callback size");
    if (@sizeOf(AbixCallbackOwned) != 3 * pointer_size + 8) @compileError("owned callback size");
}

extern fn conformance_optional(present: i32) AbixOptionalI32;
extern fn conformance_optional_valid(value: AbixOptionalI32) i32;
extern fn conformance_optional_read(value: AbixOptionalI32, out: *i32) i32;
extern fn conformance_result(value: i32) AbixResultI32;
extern fn conformance_result_valid(value: AbixResultI32) i32;
extern fn conformance_result_read(value: AbixResultI32, out: *i32) i32;
extern fn conformance_greeting() AbixStrUtf8;
extern fn conformance_string_valid(value: AbixStrUtf8) i32;
extern fn conformance_string_len(value: AbixStrUtf8) i64;
extern fn conformance_numbers() AbixSpanI32;
extern fn conformance_sum(values: AbixSpanI32) i64;

pub fn main() void {
    var value: i32 = 0;

    const some = conformance_optional(1);
    const none = conformance_optional(0);
    if (conformance_optional_valid(some) != 1) @panic("optional some valid");
    if (conformance_optional_read(some, &value) != 0 or value != 1) @panic("optional some read");
    if (conformance_optional_valid(none) != 1) @panic("optional none valid");
    if (conformance_optional_read(none, &value) != 1) @panic("optional none read");

    const invalid_discriminant = AbixOptionalI32{ .discriminant = 2, .reserved = 0, .value = 7 };
    const invalid_reserved = AbixOptionalI32{ .discriminant = 1, .reserved = 1, .value = 7 };
    if (conformance_optional_valid(invalid_discriminant) != 0) @panic("invalid discriminant accepted");
    if (conformance_optional_read(invalid_discriminant, &value) != -1) @panic("invalid discriminant read");
    if (conformance_optional_valid(invalid_reserved) != 0) @panic("invalid reserved accepted");
    if (conformance_optional_read(invalid_reserved, &value) != -1) @panic("invalid reserved read");

    const result = conformance_result(42);
    if (conformance_result_valid(result) != 1) @panic("result valid");
    if (conformance_result_read(result, &value) != 0 or value != 42) @panic("result read");
    const invalid_result = AbixResultI32{ .status = 0, .reserved = 1, .value = 42 };
    if (conformance_result_valid(invalid_result) != 0) @panic("invalid result accepted");
    if (conformance_result_read(invalid_result, &value) != -1) @panic("invalid result read");

    const greeting = conformance_greeting();
    if (conformance_string_valid(greeting) != 1) @panic("string valid");
    if (conformance_string_len(greeting) != 5) @panic("string len");
    var invalid_string = greeting;
    invalid_string.reserved = 1;
    if (conformance_string_valid(invalid_string) != 0) @panic("invalid string accepted");
    if (conformance_string_len(invalid_string) != -1) @panic("invalid string len");

    if (conformance_sum(conformance_numbers()) != 6) @panic("span sum");

    std.debug.print("zig std conformance ok\n", .{});
}
