// Rust consumer of the cmdly C facade, driven purely by the AMC-generated
// projection: owner ABI (RAII handle), a cross-language key callback, and the
// scripted read-line. Cmdly's C++ API never crosses the boundary.
#[path = "gen/amc_generated.rs"]
mod abi;

use abi::*;
use core::ffi::c_void;
use std::ffi::{CStr, CString};

fn cs(s: &str) -> CString {
    CString::new(s).unwrap()
}

extern "C" fn on_key(context: *mut c_void, key_code: core::ffi::c_uint) -> core::ffi::c_int {
    unsafe {
        let counter = context as *mut u32;
        *counter += 1;
    }
    let _ = key_code;
    0
}

fn main() {
    unsafe {
        // Owner ABI: RAII handle, released through the library's destroy entry.
        let terminal = Ownedcmdly_terminal::create(cs("hello abix").as_ptr()).expect("create");

        // Cross-language key callback with a caller-owned context.
        let mut key_count: u32 = 0;
        let handler = on_key as extern "C" fn(*mut c_void, core::ffi::c_uint) -> core::ffi::c_int;
        let rc = cmdly_on_key(
            terminal.as_ptr(),
            0,
            handler as *const () as *mut c_void,
            &mut key_count as *mut u32 as *mut c_void,
        );
        assert_eq!(rc, 0, "on_key");

        assert_eq!(cmdly_history_insert(terminal.as_ptr(), cs("old line").as_ptr()), 0);
        assert_eq!(cmdly_completion_insert(terminal.as_ptr(), cs("hello").as_ptr()), 0);

        let mut buf = [0i8; 128];
        let n = cmdly_read_line(
            terminal.as_ptr(),
            cs("> ").as_ptr(),
            buf.as_mut_ptr(),
            buf.len() as u64,
        );
        assert_eq!(n, 10, "read_line length");
        assert_eq!(CStr::from_ptr(buf.as_ptr()).to_str().unwrap(), "hello abix");

        assert_eq!(cmdly_history_length(terminal.as_ptr()), 2, "history length");
        assert!(key_count >= 10, "callback saw the keys: {key_count}");

        let out = cmdly_last_output(terminal.as_ptr());
        assert!(!out.is_null() && !CStr::from_ptr(out).to_bytes().is_empty(), "output captured");

        // The generated metadata region carries the same facts.
        let region = abi::metadata::Region::open(&abi::AMC_METADATA_REGION).expect("region");
        assert!(region.find_type_by_name("cmdly_terminal").is_some(), "opaque handle in region");
        assert!(region.find_function_by_name("cmdly_read_line").is_some(), "read_line in region");

        // `Drop` releases the handle through `cmdly_terminal_destroy`.
    }
    println!("cmdly ABIX interop (owner + callback): OK");
}
