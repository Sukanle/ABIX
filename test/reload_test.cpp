#include "test_common.hpp"
#include <thread>
#include <atomic>
#include <cstring>
#include <chrono>
#include <vector>

TEST_CASE("9.hot_reload", "[reload][prompt9]") {
    log_info(
        "Test 9: hot-reload - after unloading A and loading B the handle ID is unchanged and the return value updates");
    skl::abix::dll::Object lib;
    REQUIRE(lib.load(dll_path("reload_dll_a").c_str()));

    auto v = skl::abix::dll::Function<int()>(lib, "get_version");
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

    skl::abix::dll::Object lib;
    auto dllpath = dll_path("math_dll");
    log_info("loading %s", dllpath.c_str());
    REQUIRE(lib.load(dllpath.c_str()));
    REQUIRE(lib.is_loaded());

    const auto *t = lib.enter_read();
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
        const auto *t2 = lib.enter_read();
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
    const auto *t3 = lib.enter_read();
    REQUIRE(t3 != nullptr);
    lib.exit_read();
    log_info("load() recovers: DLL reloaded and functional");
}

TEST_CASE("26.rcu_unload_paths", "[rcu][prompt26]") {
    log_info("Test 26: RCU unload — normal unload, force unload, reload");

    SECTION("26a.normal_unload_no_readers") {
        skl::abix::dll::Object lib;
        REQUIRE(lib.load(dll_path("math_dll").c_str()));
        const auto *t = lib.enter_read();
        REQUIRE(t != nullptr);
        lib.exit_read();
        REQUIRE(lib.unload());
        REQUIRE(!lib.is_loaded());
        log_info("normal unload with no readers: success");
    }

    SECTION("26b.force_unload") {
        skl::abix::dll::Object lib;
        REQUIRE(lib.load(dll_path("math_dll").c_str()));
        lib.force_unload();
        REQUIRE(!lib.is_loaded());
        log_info("force_unload bypasses RCU: DLL unloaded immediately");
    }

    SECTION("26c.reload_after_unload") {
        skl::abix::dll::Object lib;
        REQUIRE(lib.load(dll_path("math_dll").c_str()));
        REQUIRE(lib.reload(dll_path("math_dll").c_str()));
        REQUIRE(lib.is_loaded());
        auto add = skl::abix::dll::Function<int(int, int)>(lib, "add");
        REQUIRE(add.valid());
        REQUIRE(add(2, 3) == 5);
        log_info("reload after unload: DLL functional, add(2,3)=%d", add(2, 3));
    }
}

TEST_CASE("27.logging_in_dll_operations", "[log][integration][prompt27]") {
    log_info("Test 27: logging integration — verify ABIX_LOG_* macros fire during DLL operations");

    static int log_events = 0;
    auto sink = [](skl::abix::util::LogLevel, const char *) { ++log_events; };
    skl::abix::util::set_log_sink(sink);
    log_events = 0;

    skl::abix::dll::Object lib;
    REQUIRE(lib.load(dll_path("math_dll").c_str()));
    REQUIRE(log_events > 0);
    int load_events = log_events;
    log_info("load() generated %d log events", load_events);

    {
        auto add = skl::abix::dll::Function<int(int, int)>(lib, "add");
        REQUIRE(add.valid());
        REQUIRE(add(2, 3) == 5);
    }

    REQUIRE(lib.unload());
    REQUIRE(log_events > load_events);
    log_info("unload() generated additional log events (total: %d)", log_events);

    skl::abix::util::set_log_sink(nullptr);
}

TEST_CASE("28.load_call_unload_roundtrip", "[integration][prompt28]") {
    log_info("Test 28: full roundtrip — load + calls + unload, with the logging sink installed");

    static int log_events = 0;
    auto sink = [](skl::abix::util::LogLevel, const char *) { ++log_events; };
    skl::abix::util::set_log_sink(sink);
    log_events = 0;

    skl::abix::dll::Object lib;

    REQUIRE(lib.load(dll_path("math_dll").c_str()));
    REQUIRE(lib.is_loaded());

    {
        auto add = skl::abix::dll::Function<int(int, int)>(lib, "add");
        REQUIRE(add.valid());
        REQUIRE(add(3, 4) == 7);
        REQUIRE(add(10, -2) == 8);

        auto mul = skl::abix::dll::Function<double(double, double)>(lib, "multiply");
        REQUIRE(mul.valid());
        REQUIRE(mul(2.5, 4.0) == 10.0);
    }

    REQUIRE(lib.unload());
    REQUIRE(!lib.is_loaded());
    REQUIRE(log_events > 0);

    log_info("full roundtrip: load/call/unload = %d log events", log_events);

    skl::abix::util::set_log_sink(nullptr);
}

TEST_CASE("29.force_unload_while_reader_held", "[rcu][prompt29]") {
    log_info("Test 29: force_unload detaches and frees the image immediately while a reader is held");

    skl::abix::dll::Object lib;
    REQUIRE(lib.load(dll_path("math_dll").c_str()));
    REQUIRE(lib.is_loaded());

    std::atomic<bool> reader_active{false};
    std::atomic<bool> reader_done{false};
    std::thread reader([&]() {
        const auto *t = lib.enter_read();
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
    auto add = skl::abix::dll::Function<int(int, int)>(lib, "add");
    REQUIRE(add.valid());
    REQUIRE(add(1, 2) == 3);
    log_info("reload after ForceUnload: DLL functional, add(1,2)=%d", add(1, 2));
}

TEST_CASE("31.unload_blocks_until_reader_exits", "[rcu][prompt31]") {
    log_info("Test 31: unload() blocks until the reader leaves, and the module is usable again afterwards");

    skl::abix::dll::Object lib;
    REQUIRE(lib.load(dll_path("math_dll").c_str()));
    REQUIRE(lib.is_loaded());

    std::atomic<bool> reader_active{false};
    std::atomic<bool> unload_done{false};
    std::atomic<bool> reader_done{false};

    std::thread reader_thread([&]() {
        const auto *t = lib.enter_read();
        REQUIRE(t != nullptr);
        reader_active.store(true);
        // No timeout exists: the grace period is unbounded, so this reader holds
        // unload() for as long as it stays inside.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
    auto add = skl::abix::dll::Function<int(int, int)>(lib, "add");
    REQUIRE(add.valid());
    REQUIRE(add(1, 2) == 3);
    log_info("reload after unload: DLL functional, add(1,2)=%d", add(1, 2));
}

// The cached ("verified snapshot") call path in Function::operator() must not
// outlive the image it was validated against. These three cases are the
// regression guard for that optimization.
TEST_CASE("9b.a cached handle never calls a stale image", "[reload][prompt9b]") {
    log_info("Test 9b: the cached call path must follow a reload and must not survive an unload");

    skl::abix::dll::Object lib;
    REQUIRE(lib.load(dll_path("reload_dll_a").c_str()));

    auto helper = skl::abix::dll::Function<int(int)>(lib, "helper");
    REQUIRE(helper.valid());
    REQUIRE(helper(5) == 105);
    REQUIRE(helper(5) == 105);   // now served by the cached path
    log_info("reload_dll_a: helper(5)=105 (twice, cached path established)");

    // Positive: after the swap the same handle must reach the NEW implementation.
    REQUIRE(lib.reload(dll_path("reload_dll_b").c_str()));
    REQUIRE(lib.is_loaded());
    REQUIRE(helper(5) == 205);
    REQUIRE(helper(5) == 205);
    log_info("after reload to reload_dll_b: helper(5)=205 (stable pointer not reused)");

    // Negative: after the module is force-unloaded the handle must refuse
    // instead of calling into a module that is on its way out. (`unload()`
    // refuses while a handle is alive, so the forced path is the one that can
    // strand a handle.)
    lib.force_unload();
    REQUIRE(!lib.is_loaded());
    REQUIRE(helper(5) == 0);
    REQUIRE(skl::abix::dll::last_error() == skl::abix::dll::CallError::not_loaded);
    log_info("after force_unload: helper(5) refused with not_loaded");
}

TEST_CASE("9c.a handle resolved against one module rejects another module's table", "[reload][prompt9c]") {
    log_info("Test 9c: re-pointing the Object at a different module must fail the stale handle");

    skl::abix::dll::Object lib;
    REQUIRE(lib.load(dll_path("math_dll").c_str()));
    auto add = skl::abix::dll::Function<int(int, int)>(lib, "add");
    REQUIRE(add.valid());
    REQUIRE(add(2, 3) == 5);

    REQUIRE(lib.reload(dll_path("edge_dll").c_str()));
    REQUIRE(lib.is_loaded());

    const int refused = add(2, 3);
    REQUIRE(refused == 0);
    REQUIRE(skl::abix::dll::last_error() == skl::abix::dll::CallError::table_changed);
    log_info("stale handle refused after the module swap (result=%d, error=table_changed)", refused);
}

TEST_CASE("9d.concurrent calls through a cached handle", "[reload][threads][prompt9d]") {
    log_info("Test 9d: concurrent callers share the cached snapshot without a wrong call");

    skl::abix::dll::Object lib;
    REQUIRE(lib.load(dll_path("math_dll").c_str()));
    auto add = skl::abix::dll::Function<int(int, int)>(lib, "add");
    REQUIRE(add.valid());
    REQUIRE(add(1, 1) == 2);   // establish the snapshot before the threads start

    std::atomic<int> wrong{0};
    std::vector<std::thread> threads;
    threads.reserve(8);
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&add, &wrong] {
            for (int i = 0; i < 20'000; ++i)
                if (add(i, 1) != i + 1) ++wrong;
        });
    }
    for (auto &thread : threads)
        thread.join();

    REQUIRE(wrong.load() == 0);
    log_info("8 threads x 20000 calls through the cached path: 0 wrong results");
}

