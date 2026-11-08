// Rust consumer for the ABIX-STD v0.4.0 conformance fixture.
//
// Rust's `Option<T>`, `Result<T, E>` and `&str` are not layout-equivalent to
// ABIX-STD. This consumer therefore declares the explicit `#[repr(C)]` adapter
// and checks size/alignment/offset before making any call.

#[repr(C)]
#[derive(Clone, Copy)]
struct AbixOptionalI32 {
    discriminant: u32,
    reserved: u32,
    value: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct AbixResultI32 {
    status: i32,
    reserved: u32,
    value: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct AbixStrUtf8 {
    data: *const u8,
    len: u64,
    encoding: u32,
    reserved: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct AbixSpanI32 {
    data: *const i32,
    len: u64,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct AbixCallback {
    invoke: Option<unsafe extern "C" fn(*mut core::ffi::c_void) -> i32>,
    context: *mut core::ffi::c_void,
}

type AbixCallbackReleaseFn = Option<unsafe extern "C" fn(*mut core::ffi::c_void)>;

#[repr(C)]
#[derive(Clone, Copy)]
struct AbixCallbackOwned {
    invoke: Option<unsafe extern "C" fn(*mut core::ffi::c_void) -> i32>,
    context: *mut core::ffi::c_void,
    release: AbixCallbackReleaseFn,
    flags: u32,
    reserved: u32,
}

const fn align_up(value: usize, align: usize) -> usize {
    if align == 0 {
        value
    } else {
        (value + align - 1) / align * align
    }
}

const fn max(a: usize, b: usize) -> usize {
    if a > b {
        a
    } else {
        b
    }
}

const POINTER_SIZE: usize = core::mem::size_of::<*const u8>();
const STRING_LEN_OFFSET: usize = align_up(POINTER_SIZE, core::mem::align_of::<u64>());
const STRING_ENCODING_OFFSET: usize = align_up(STRING_LEN_OFFSET + 8, core::mem::align_of::<u32>());
const STRING_RESERVED_OFFSET: usize = STRING_ENCODING_OFFSET + 4;
const STRING_ALIGN: usize = max(max(POINTER_SIZE, core::mem::align_of::<u64>()), core::mem::align_of::<u32>());
const STRING_SIZE: usize = align_up(STRING_RESERVED_OFFSET + 4, STRING_ALIGN);

const _: () = assert!(core::mem::size_of::<AbixOptionalI32>() == 12);
const _: () = assert!(core::mem::align_of::<AbixOptionalI32>() == 4);
const _: () = assert!(core::mem::offset_of!(AbixOptionalI32, discriminant) == 0);
const _: () = assert!(core::mem::offset_of!(AbixOptionalI32, reserved) == 4);
const _: () = assert!(core::mem::offset_of!(AbixOptionalI32, value) == 8);

const _: () = assert!(core::mem::size_of::<AbixResultI32>() == 12);
const _: () = assert!(core::mem::align_of::<AbixResultI32>() == 4);
const _: () = assert!(core::mem::offset_of!(AbixResultI32, status) == 0);
const _: () = assert!(core::mem::offset_of!(AbixResultI32, reserved) == 4);
const _: () = assert!(core::mem::offset_of!(AbixResultI32, value) == 8);

const _: () = assert!(core::mem::offset_of!(AbixStrUtf8, data) == 0);
const _: () = assert!(core::mem::offset_of!(AbixStrUtf8, len) == STRING_LEN_OFFSET);
const _: () = assert!(core::mem::offset_of!(AbixStrUtf8, encoding) == STRING_ENCODING_OFFSET);
const _: () = assert!(core::mem::offset_of!(AbixStrUtf8, reserved) == STRING_RESERVED_OFFSET);
const _: () = assert!(core::mem::size_of::<AbixStrUtf8>() == STRING_SIZE);
const _: () = assert!(core::mem::align_of::<AbixStrUtf8>() == STRING_ALIGN);

const _: () = assert!(core::mem::size_of::<AbixSpanI32>() == 2 * POINTER_SIZE);
const _: () = assert!(core::mem::size_of::<AbixCallback>() == 2 * POINTER_SIZE);
const _: () = assert!(core::mem::size_of::<AbixCallbackOwned>() == 3 * POINTER_SIZE + 8);

#[link(name = "conformance_std")]
extern "C" {
    fn conformance_optional(present: i32) -> AbixOptionalI32;
    fn conformance_optional_valid(value: AbixOptionalI32) -> i32;
    fn conformance_optional_read(value: AbixOptionalI32, out: *mut i32) -> i32;
    fn conformance_result(value: i32) -> AbixResultI32;
    fn conformance_result_valid(value: AbixResultI32) -> i32;
    fn conformance_result_read(value: AbixResultI32, out: *mut i32) -> i32;
    fn conformance_greeting() -> AbixStrUtf8;
    fn conformance_string_valid(value: AbixStrUtf8) -> i32;
    fn conformance_string_len(value: AbixStrUtf8) -> i64;
    fn conformance_numbers() -> AbixSpanI32;
    fn conformance_sum(values: AbixSpanI32) -> i64;
}

fn main() {
    unsafe {
        let some = conformance_optional(1);
        let none = conformance_optional(0);
        let mut value = 0i32;
        assert_eq!(conformance_optional_valid(some), 1);
        assert_eq!(conformance_optional_read(some, &mut value), 0);
        assert_eq!(value, 1);
        assert_eq!(conformance_optional_valid(none), 1);
        assert_eq!(conformance_optional_read(none, &mut value), 1);

        let invalid_discriminant = AbixOptionalI32 {
            discriminant: 2,
            reserved: 0,
            value: 7,
        };
        let invalid_reserved = AbixOptionalI32 {
            discriminant: 1,
            reserved: 1,
            value: 7,
        };
        assert_eq!(conformance_optional_valid(invalid_discriminant), 0);
        assert_eq!(conformance_optional_read(invalid_discriminant, &mut value), -1);
        assert_eq!(conformance_optional_valid(invalid_reserved), 0);
        assert_eq!(conformance_optional_read(invalid_reserved, &mut value), -1);

        let result = conformance_result(42);
        assert_eq!(conformance_result_valid(result), 1);
        assert_eq!(conformance_result_read(result, &mut value), 0);
        assert_eq!(value, 42);
        let invalid_result = AbixResultI32 {
            status: 0,
            reserved: 1,
            value: 42,
        };
        assert_eq!(conformance_result_valid(invalid_result), 0);
        assert_eq!(conformance_result_read(invalid_result, &mut value), -1);

        let greeting = conformance_greeting();
        assert_eq!(conformance_string_valid(greeting), 1);
        assert_eq!(conformance_string_len(greeting), 5);
        let mut invalid_string = greeting;
        invalid_string.reserved = 1;
        assert_eq!(conformance_string_valid(invalid_string), 0);
        assert_eq!(conformance_string_len(invalid_string), -1);

        assert_eq!(conformance_sum(conformance_numbers()), 6);
    }

    println!("rust std conformance ok");
}
