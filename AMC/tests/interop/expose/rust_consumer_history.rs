// Rust consumer of a real cmdly type whose `insert` is inherited from the base
// class and takes `const std::string&`. With a declared [[adapt]] the frontend
// exposes it with an ABIX-STD StringView parameter and generates the adapter,
// so Rust drives the real cmdly history end to end.
#[path = "gen/amc_generated.rs"]
mod abi;
use abi::*;

fn text_view(bytes: &[u8]) -> abix_str_utf8 {
    abix_str_utf8 {
        data: bytes.as_ptr() as *const core::ffi::c_char,
        len: bytes.len() as u64,
        encoding: 0,
    }
}

fn main() {
    unsafe {
        let history = Ownedcmdly__MemoryHistory::create().expect("create");
        abix_cmdly__MemoryHistory_insert(history.as_ptr(), text_view(b"first"));
        abix_cmdly__MemoryHistory_insert(history.as_ptr(), text_view(b"second"));
        // The same line is dropped (history keeps unique, most-recent-first).
        abix_cmdly__MemoryHistory_insert(history.as_ptr(), text_view(b"second"));
        assert_eq!(abix_cmdly__MemoryHistory_length(history.as_ptr()), 2, "two lines");
    }
    println!("expose history: OK");
}
