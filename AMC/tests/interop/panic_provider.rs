// Test-only provider for the Rust panic / extern "C" boundary test.
//
// `abix_catch_panic` is the supported shape: a provider catches its own panic
// and returns an explicit status. `abix_panic_now` is the negative control: a
// panic escaping an `extern "C"` function must abort at the boundary instead of
// unwinding into the foreign caller.

#[no_mangle]
pub extern "C" fn abix_catch_panic() -> i32 {
    match std::panic::catch_unwind(|| panic!("intentional panic caught at the provider boundary")) {
        Ok(()) => 0,
        Err(_) => 77,
    }
}

#[no_mangle]
pub extern "C" fn abix_panic_now() {
    panic!("intentional uncaught panic");
}
