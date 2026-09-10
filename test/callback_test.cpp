#include "test_common.hpp"

TEST_CASE("6.abi_function_callback", "[callback][prompt6]") {
    log_info(
        "Test 6: skl::abix::function_dll (8-byte callback) - a capturing lambda is registered across the boundary and "
        "invoked lazily by the DLL");
    static_assert(
        sizeof(skl::abix::function_dll<void(int)>) == 8, "skl::abix::function_dll must be 8 bytes (uint 64_t handle)");
    log_info("sizeof(skl::abix::function_dll<void(int)>) = %zu bytes (8-byte handle)",
        sizeof(skl::abix::function_dll<void(int)>));
    skl::abix::dll_object lib;
    REQUIRE(lib.load(dll_path("callback_dll").c_str()));

    auto reg = skl::abix::dll_func<void(skl::abix::function_dll<void(int)>)>(lib, "register_callback");
    auto has = skl::abix::dll_func<int()>(lib, "has_callback");
    auto inv = skl::abix::dll_func<int(int)>(lib, "invoke_callback");
    auto last = skl::abix::dll_func<int()>(lib, "get_last_invoked");
    auto clear = skl::abix::dll_func<void()>(lib, "clear_callback");
    REQUIRE(reg.valid());

    int captured = 100;
    skl::abix::function_dll<void(int)> cb = [captured](int x) {
        log_info("callback invoked: captured variable captured=%d, argument x=%d, sum=%d", captured, x, captured + x);
    };
    REQUIRE(cb);
    reg(std::move(cb));
    REQUIRE(has() == 1);
    log_info("callback registered into the DLL (has_callback=1), handle=%llu", (unsigned long long)cb.handle());

    REQUIRE(inv(5) == 0);
    REQUIRE(last() == 5);
    log_info(
        "DLL lazily invoked the callback inv(5)=0; the captured variable was copied/moved correctly, last_invoked=5");

    clear();
    REQUIRE(has() == 0);
    REQUIRE(inv(1) == -1);
    log_info("after clear_callback has_callback=0, subsequent invocation is rejected (returns -1)");
}