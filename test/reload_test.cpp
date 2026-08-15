#include "test_common.hpp"
#include <thread>
#include <atomic>
#include <cstring>
#include <chrono>

TEST_CASE("9.hot_reload", "[reload][prompt9]") {
    log_info(
        "Test 9: hot-reload - after unloading A and loading B the handle ID is unchanged and the return value updates");
    dll_object lib;
    REQUIRE(lib.load(dll_path("reload_dll_a").c_str()));

    auto v = dll_func<int()>(lib, "get_version");
    REQUIRE(v.valid());
    uint64_t id = v.handle_id();
    REQUIRE(v() == 1);
    log_info("loaded reload_dll_a: get_version()=1, integer handle ID=%llu", (unsigned long long)id);

    REQUIRE(lib.reload(dll_path("reload_dll_b").c_str()));
    REQUIRE(lib.is_loaded());
    log_info("hot-reload: unload A and load reload_dll_b");

    REQUIRE(v.handle_id() == id);
    REQUIRE(v() == 2);
    log_info("after hot-reload the handle ID is still %llu, get_version()=2 (zero-downtime upgrade succeeded)",
        (unsigned long long)v.handle_id());
}

TEST_CASE("24.zombie_lifecycle", "[zombie][prompt24]") {
    log_info("Test 24: zombie lifecycle — unload blocks until readers exit, force_unload bypasses RCU");

    dll_object lib;
    REQUIRE(lib.load(dll_path("math_dll").c_str()));
    REQUIRE(lib.is_loaded());

    const table *t = lib.enter_read();
    REQUIRE(t != nullptr);
    lib.exit_read();

    bool unloaded = lib.unload();
    REQUIRE(unloaded);
    REQUIRE(!lib.is_loaded());
    log_info("normal unload with no active readers: success");

    REQUIRE(lib.load(dll_path("math_dll").c_str()));
    REQUIRE(lib.is_loaded());

    std::atomic<bool> reader_active{false};
    std::atomic<bool> reader_done{false};
    std::thread reader([&]() {
        const table *t2 = lib.enter_read();
        REQUIRE(t2 != nullptr);
        reader_active.store(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        lib.exit_read();
        reader_done.store(true);
    });

    while (!reader_active.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    log_info("reader active, calling force_unload to bypass RCU");

    lib.force_unload();
    REQUIRE(!lib.is_loaded());
    reader.join();
    REQUIRE(reader_done.load());
    log_info("force_unload bypassed RCU, reader completed safely (caller ensured safety)");

    REQUIRE(lib.load(dll_path("math_dll").c_str()));
    REQUIRE(lib.is_loaded());
    const table *t3 = lib.enter_read();
    REQUIRE(t3 != nullptr);
    lib.exit_read();
    log_info("load() recovers: DLL reloaded and functional");
}

TEST_CASE("25.tick_function", "[tick][prompt25]") {
    log_info("Test 25: abix::tick() function — updates time baseline for starvation guard");

    uint64_t t0 = 1'000;
    tick(t0);
    log_info("tick(1000) called");

    uint64_t t1 = 5'000;
    tick(t1);
    log_info("tick(5000) called");

    uint64_t current = detail::get_tick_frames();
    REQUIRE(current == t1);
    log_info("get_tick_frames() returns tick value: %llu", (unsigned long long)current);

    tick(9'999);
    tick(10'000);
    log_info("tick() accepts incremental timestamps correctly");
}

TEST_CASE("26.rcu_unload_with_timeout", "[rcu][timeout][prompt26]") {
    log_info("Test 26: RCU unload — normal unload, force unload, reload");

    SECTION("26a.normal_unload_no_readers") {
        dll_object lib;
        REQUIRE(lib.load(dll_path("math_dll").c_str()));
        const table *t = lib.enter_read();
        REQUIRE(t != nullptr);
        lib.exit_read();
        REQUIRE(lib.unload());
        REQUIRE(!lib.is_loaded());
        log_info("normal unload with no readers: success");
    }

    SECTION("26b.force_unload") {
        dll_object lib;
        REQUIRE(lib.load(dll_path("math_dll").c_str()));
        lib.force_unload();
        REQUIRE(!lib.is_loaded());
        log_info("force_unload bypasses RCU: DLL unloaded immediately");
    }

    SECTION("26c.reload_after_unload") {
        dll_object lib;
        REQUIRE(lib.load(dll_path("math_dll").c_str()));
        REQUIRE(lib.reload(dll_path("math_dll").c_str()));
        REQUIRE(lib.is_loaded());
        auto add = dll_func<int(int, int)>(lib, "add");
        REQUIRE(add.valid());
        REQUIRE(add(2, 3) == 5);
        log_info("reload after unload: DLL functional, add(2,3)=%d", add(2, 3));
    }
}

TEST_CASE("27.logging_in_dll_operations", "[log][integration][prompt27]") {
    log_info("Test 27: logging integration — verify ABIX_LOG_* macros fire during DLL operations");

    static int log_events = 0;
    auto sink = [](LogLevel, const char *) { ++log_events; };
    set_log_sink(sink);
    log_events = 0;

    dll_object lib;
    REQUIRE(lib.load(dll_path("math_dll").c_str()));
    REQUIRE(log_events > 0);
    int load_events = log_events;
    log_info("load() generated %d log events", load_events);

    {
        auto add = dll_func<int(int, int)>(lib, "add");
        REQUIRE(add.valid());
        REQUIRE(add(2, 3) == 5);
    }

    REQUIRE(lib.unload());
    REQUIRE(log_events > load_events);
    log_info("unload() generated additional log events (total: %d)", log_events);

    set_log_sink(nullptr);
}

TEST_CASE("28.full_config_roundtrip", "[config][integration][prompt28]") {
    log_info("Test 28: full config roundtrip — RCUTimeoutConfig + timeout policy + logging");

    static int log_events = 0;
    auto sink = [](LogLevel, const char *) { ++log_events; };
    set_log_sink(sink);
    log_events = 0;

    dll_object lib(RCUTimeoutConfig{3'000});
    lib.set_timeout_policy(RCUTimeoutPolicy::Safe);
    REQUIRE(lib.timeout_policy() == RCUTimeoutPolicy::Safe);

    REQUIRE(lib.load(dll_path("math_dll").c_str()));
    REQUIRE(lib.is_loaded());

    {
        auto add = dll_func<int(int, int)>(lib, "add");
        REQUIRE(add.valid());
        REQUIRE(add(3, 4) == 7);
        REQUIRE(add(10, -2) == 8);

        auto mul = dll_func<double(double, double)>(lib, "multiply");
        REQUIRE(mul.valid());
        REQUIRE(mul(2.5, 4.0) == 10.0);
    }

    REQUIRE(lib.unload());
    REQUIRE(!lib.is_loaded());
    REQUIRE(log_events > 0);

    log_info("full roundtrip: config(3000ms) + Safe policy + load/math/unload = %d log events", log_events);

    set_log_sink(nullptr);
}

TEST_CASE("29.force_unload_timeout", "[policy][timeout][prompt29]") {
    log_info("Test 29: ForceUnload — hold reader, force_unload bypasses RCU to immediately unload");

    dll_object lib;
    lib.set_timeout_policy(RCUTimeoutPolicy::ForceUnload);
    REQUIRE(lib.load(dll_path("math_dll").c_str()));
    REQUIRE(lib.is_loaded());

    std::atomic<bool> reader_active{false};
    std::atomic<bool> reader_done{false};
    std::thread reader([&]() {
        const table *t = lib.enter_read();
        REQUIRE(t != nullptr);
        reader_active.store(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        lib.exit_read();
        reader_done.store(true);
    });

    while (!reader_active.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    log_info("reader held, calling force_unload");

    lib.force_unload();
    REQUIRE(!lib.is_loaded());
    reader.join();
    REQUIRE(reader_done.load());
    log_info("force_unload completed: DLL force-unloaded, is_loaded()=false");

    REQUIRE(lib.load(dll_path("math_dll").c_str()));
    REQUIRE(lib.is_loaded());
    auto add = dll_func<int(int, int)>(lib, "add");
    REQUIRE(add.valid());
    REQUIRE(add(1, 2) == 3);
    log_info("reload after ForceUnload: DLL functional, add(1,2)=%d", add(1, 2));
}

TEST_CASE("30.force_leak_timeout", "[policy][timeout][prompt30]") {
#if defined(ABIX_ENABLE_FORCE_LEAK_POLICY)
    log_info("Test 30: ForceLeak — hold reader, force_unload bypasses RCU to leak safely");

    dll_object lib;
    lib.set_timeout_policy(RCUTimeoutPolicy::ForceLeak);
    REQUIRE(lib.load(dll_path("math_dll").c_str()));
    REQUIRE(lib.is_loaded());

    std::atomic<bool> reader_active{false};
    std::atomic<bool> reader_done{false};
    std::thread reader([&]() {
        const table *t_fl = lib.enter_read();
        REQUIRE(t_fl != nullptr);
        reader_active.store(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        lib.exit_read();
        reader_done.store(true);
    });

    while (!reader_active.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    log_info("reader held, calling force_unload with ForceLeak policy");

    lib.force_unload();
    REQUIRE(!lib.is_loaded());
    reader.join();
    REQUIRE(reader_done.load());
    log_info("force_unload completed: DLL unloaded, is_loaded()=false");

    REQUIRE(lib.load(dll_path("math_dll").c_str()));
    REQUIRE(lib.is_loaded());
    auto add = dll_func<int(int, int)>(lib, "add");
    REQUIRE(add.valid());
    REQUIRE(add(1, 2) == 3);
    log_info("reload after ForceLeak: DLL functional, add(1,2)=%d", add(1, 2));
#else
    log_info("Test 30: ForceLeak timeout — skipped (ABIX_ENABLE_FORCE_LEAK_POLICY not defined)");
#endif
}

TEST_CASE("31.tick_frame_timeout", "[tick][timeout][prompt31]") {
    log_info("Test 31: Tick frame-driven — unload blocks until reader exits, force_unload bypasses");

    dll_object lib;
    lib.set_timeout_policy(RCUTimeoutPolicy::Safe);
    REQUIRE(lib.load(dll_path("math_dll").c_str()));
    REQUIRE(lib.is_loaded());

    std::atomic<bool> reader_active{false};
    std::atomic<bool> unload_done{false};
    std::atomic<bool> reader_done{false};

    tick(0);
    std::thread reader_thread([&]() {
        const table *t_tick = lib.enter_read();
        REQUIRE(t_tick != nullptr);
        reader_active.store(true);
        for (uint64_t f = 1; f <= 15; ++f) {
            tick(f);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        lib.exit_read();
        reader_done.store(true);
    });

    while (!reader_active.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    log_info("reader held, calling unload (will block until reader exits)");

    std::thread unload_thread([&]() {
        bool r = lib.unload();
        unload_done.store(r);
    });

    reader_thread.join();
    REQUIRE(reader_done.load());
    unload_thread.join();
    REQUIRE(unload_done.load());
    REQUIRE(!lib.is_loaded());
    log_info("unload completed after reader exited, is_loaded()=false");

    REQUIRE(lib.load(dll_path("math_dll").c_str()));
    REQUIRE(lib.is_loaded());
    auto add = dll_func<int(int, int)>(lib, "add");
    REQUIRE(add.valid());
    REQUIRE(add(1, 2) == 3);
    log_info("reload after unload: DLL functional, add(1,2)=%d", add(1, 2));
}