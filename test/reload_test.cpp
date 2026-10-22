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

// The slot-buffer pool in ABIX/Runtime/Search.h only retains a buffer for tables
// at or above SKL_ABIX_HASH_POOL_MIN_SLOTS (4096 slots = 2048 entries), and the
// largest test module (hotcache_dll, 1020 entries -> 2048 slots) sits just below
// that. So this case is the lifecycle guard *around* the pool - the module really
// does build a hash index, and its buffer travels through the same reclaim path -
// while the recycling branch itself is covered deterministically in pool_test.
// What is new here, against 9d above, is readers running *during* the reloads:
// resolves and calls happen while images are being swapped and reclaimed.
TEST_CASE("43.reload_an_indexed_module_under_concurrent_calls", "[reload][pool][threads]") {
    log_info("Test 43: repeated reloads of an index-bearing module with concurrent readers");

    const std::string path = dll_path("hotcache_dll");
    skl::abix::dll::Object lib;
    REQUIRE(lib.load(path.c_str()));
    REQUIRE(lib.is_loaded());
    const auto *table = lib.get_table();
    REQUIRE(table != nullptr);
    REQUIRE(table->count >= SKL_ABIX_HASH_THRESHOLD);   // this module really does build an index
    const uint32_t slots = skl::abix::runtime::next_pow2(table->count * 2);
    REQUIRE(slots < SKL_ABIX_HASH_POOL_MIN_SLOTS);   // ... one the pool deliberately does not retain
    log_info("hotcache_dll: %u entries -> an index of %u slots (%u below the pool floor)", table->count, slots,
        SKL_ABIX_HASH_POOL_MIN_SLOTS - slots);

    // The helpers in test_common.hpp format into one shared static buffer, which
    // four threads cannot use at once: keep the name storage local and stable for
    // as long as the handle lives (Function stores the pointer, not the text).
    const std::string hot_entry = hot_name(0);
    auto hot = skl::abix::dll::Function<int(int)>(lib, hot_entry.c_str());
    REQUIRE(hot.valid());
    REQUIRE(hot(5) == 5);

    std::atomic<int> wrong{0};
    std::atomic<bool> stop{false};
    std::vector<std::thread> readers;
    readers.reserve(4);
    for (int t = 0; t < 4; ++t) {
        readers.emplace_back([&lib, &hot, &wrong, &stop, t] {
            const int n = 100 + t;
            char name[16];
            std::snprintf(name, sizeof(name), "e_%04d", n);
            while (!stop.load(std::memory_order_relaxed)) {
                // The cached call path: `hot` names an entry of this module, so
                // after a same-path swap it must still reach that entry.
                if (hot(9) != 9) ++wrong;

                // Resolve a cold name (hash-index path) and call it. A reload can
                // land between the two, and then the call *must* be refused (0)
                // rather than reach the new image through a stale index: that
                // refusal is the contract, so only a third value is a bug.
                auto cold = skl::abix::dll::Function<int(int)>(lib, name);
                if (cold.valid()) {
                    const int got = cold(2);
                    if (got != 0 && got != 2 + n) ++wrong;
                }
            }
        });
    }

    for (int i = 0; i < 50; ++i) {
        if (!lib.reload(path.c_str())) {
            ++wrong;
            break;
        }
    }
    stop.store(true);
    for (auto &thread : readers)
        thread.join();

    REQUIRE(lib.is_loaded());
    REQUIRE(wrong.load() == 0);
    REQUIRE(hot(3) == 3);
    auto cold = skl::abix::dll::Function<int(int)>(lib, cold_name(42));
    REQUIRE(cold.valid());
    REQUIRE(cold(1) == 43);
    log_info("50 reloads with 4 concurrent readers: 0 wrong results, cold lookups still resolve");
}

// --- Reclaim timeout: three failure modes, tested separately -------------------
//
// The three have different root causes and different expected behaviour, so they are
// deliberately not folded into one test: a reader that never leaves (46a), the
// normal paths with and without a bound (46b), and many writers competing for the
// grace period while a reader is stuck (46c).

// (1) A reader that never leaves. `reload()` must still complete - the swap happens
//     before the wait - report the deferral, and keep the old image mapped until a
//     later grace period reclaims it (the "safe" policy: a timeout never unloads).
TEST_CASE("46a.a stuck reader defers reclamation without blocking reload", "[reload][rcu][reclaim]") {
    log_info("Test 46a: bounded reclaim with a reader that never leaves");

    const std::string path = dll_path("math_dll");
    skl::abix::dll::Object lib;
    REQUIRE(lib.load(path.c_str()));
    auto add = skl::abix::dll::Function<int(int, int)>(lib, "add");
    REQUIRE(add.valid());
    REQUIRE(add(1, 2) == 3);

    REQUIRE(lib.reclaim_timeout_ms() == 0);   // unbounded by default
    lib.set_reclaim_timeout_ms(5);
    REQUIRE(lib.reclaim_timeout_ms() == 5);

    std::atomic<bool> inside{false};
    std::atomic<bool> release{false};
    std::thread reader([&] {
        const auto *t = lib.enter_read();
        REQUIRE(t != nullptr);
        inside.store(true);
        while (!release.load())
            std::this_thread::yield();
        lib.exit_read();
    });
    while (!inside.load())
        std::this_thread::yield();

    int deferred = 0;
    for (int i = 0; i < 5; ++i) {
        REQUIRE(lib.reload(path.c_str()));
        REQUIRE(lib.is_loaded());
        if (lib.reclaim_outcome() == skl::abix::dll::ReclaimOutcome::deferred_timeout) ++deferred;
        REQUIRE(add(2, 3) == 5);   // the handle stays correct through every swap
    }
    REQUIRE(deferred == 5);

    release.store(true);
    reader.join();

    REQUIRE(lib.reload(path.c_str()));
    REQUIRE(lib.reclaim_outcome() == skl::abix::dll::ReclaimOutcome::reclaimed);
    REQUIRE(add(4, 4) == 8);
    log_info("5 reloads while a reader was held: all deferred, then reclaimed once it left");
}

// (2) The normal paths. A bounded reclaim must reclaim on every operation (so the
//     retired list cannot accumulate); the default path keeps the batched cadence,
//     which defers by design.
TEST_CASE("46b.bounded reclaims every time, the default batches", "[reload][rcu][reclaim]") {
    SECTION("46b1.bounded: every reload reclaims") {
        const std::string path = dll_path("math_dll");
        skl::abix::dll::Object lib;
        REQUIRE(lib.load(path.c_str()));
        lib.set_reclaim_timeout_ms(50);

        int reclaimed = 0;
        for (int i = 0; i < 10; ++i) {
            REQUIRE(lib.reload(path.c_str()));
            if (lib.reclaim_outcome() == skl::abix::dll::ReclaimOutcome::reclaimed) ++reclaimed;
        }
        REQUIRE(reclaimed == 10);
        log_info("bounded mode: 10/10 reloads reclaimed the previous image (no accumulation)");
    }

    SECTION("46b2.default: the batched schedule defers between releases") {
        const std::string path = dll_path("math_dll");
        skl::abix::dll::Object lib;
        REQUIRE(lib.load(path.c_str()));
        REQUIRE(lib.reclaim_timeout_ms() == 0);

        int reclaimed = 0;
        bool saw_deferred = false;
        for (int i = 0; i < 8; ++i) {
            REQUIRE(lib.reload(path.c_str()));
            const auto outcome = lib.reclaim_outcome();
            // No timeout is configured, so a timeout can never be reported.
            REQUIRE(outcome != skl::abix::dll::ReclaimOutcome::deferred_timeout);
            if (outcome == skl::abix::dll::ReclaimOutcome::reclaimed) ++reclaimed;
            if (outcome == skl::abix::dll::ReclaimOutcome::deferred_batched) saw_deferred = true;
        }
        // 8 consecutive batched synchronizations contain at most one release, so both
        // the deferral and a release must show up.
        REQUIRE(reclaimed >= 1);
        REQUIRE(saw_deferred);
        log_info("default mode: %d/8 reclaimed, deferral observed (no timeouts)", reclaimed);
    }
}

// (3) Many writers competing for the grace period while a reader is stuck. One Object
//     per thread, because concurrent reload of the *same* Object is not an API
//     promise; the RCU domain is shared, which is what this stresses.
TEST_CASE("46c.multi-writer bounded reclaims with a stuck reader", "[reload][rcu][reclaim][threads]") {
    constexpr int kThreads = 4;
    constexpr int kRounds = 5;
    const std::string path = dll_path("math_dll");

    skl::abix::dll::Object lib;
    REQUIRE(lib.load(path.c_str()));

    std::atomic<bool> inside{false};
    std::atomic<bool> release{false};
    std::thread reader([&] {
        const auto *t = lib.enter_read();
        REQUIRE(t != nullptr);
        inside.store(true);
        while (!release.load())
            std::this_thread::yield();
        lib.exit_read();
    });
    while (!inside.load())
        std::this_thread::yield();

    std::atomic<int> failures{0};
    std::atomic<int> deferred{0};
    std::vector<std::thread> writers;
    writers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        writers.emplace_back([&] {
            skl::abix::dll::Object own;
            if (!own.load(path.c_str())) {
                ++failures;
                return;
            }
            own.set_reclaim_timeout_ms(2);
            for (int r = 0; r < kRounds; ++r) {
                if (!own.reload(path.c_str())) ++failures;
                if (own.reclaim_outcome() == skl::abix::dll::ReclaimOutcome::deferred_timeout) ++deferred;
            }
        });
    }
    for (auto &writer : writers)
        writer.join();

    REQUIRE(failures.load() == 0);
    REQUIRE(deferred.load() > 0);   // the stuck reader makes every bounded wait expire

    release.store(true);
    reader.join();

    // A final bounded operation must still be able to prove a grace period. If the
    // timeout path had wedged the `sync` handshake, this would time out instead of
    // reclaiming.
    skl::abix::dll::Object final_lib;
    REQUIRE(final_lib.load(path.c_str()));
    final_lib.set_reclaim_timeout_ms(2'000);
    REQUIRE(final_lib.reload(path.c_str()));
    REQUIRE(final_lib.reclaim_outcome() == skl::abix::dll::ReclaimOutcome::reclaimed);
    log_info(
        "%d writers x %d bounded reloads under a stuck reader: 0 failures, handshake intact, "
        "final reclaim succeeded",
        kThreads, kRounds);
}
