// Rust consumer of a real cmdly type (cmdly::Key) exposed automatically: the
// thunks, the discovery manifest and these bindings are all generated.
#[path = "gen/amc_generated.rs"]
mod abi;
use abi::*;

fn main() {
    unsafe {
        let key = Ownedcmdly__Key::create(b'A' as core::ffi::c_char).expect("create");
        assert_eq!(abix_cmdly__Key_code(key.as_ptr()) as u8, b'A', "code");
        assert!(abix_cmdly__Key_isPrintable(key.as_ptr()), "printable");
    }
    println!("expose cmdly: OK");
}
