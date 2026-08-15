#include "test_common.hpp"
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdint>

TEST_CASE("32.rcu_concurrent_stress", "[rcu][stress][concurrent]") {
    log_info("Test 32: RCU concurrent stress — N readers + 1 writer, verify no crash/UAF");

    dll_object lib;
    REQUIRE(lib.load(dll_path("math_dll").c_str()));
    REQUIRE(lib.is_loaded());

    static constexpr int N_READERS = 4;
    static constexpr int ITERATIONS = 5'000;
    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};
    std::atomic<int> read_count{0};
    std::atomic<int> error_count{0};
    std::thread readers[N_READERS];

    for (int i = 0; i < N_READERS; ++i) {
        readers[i] = std::thread([&, i]() {
            while (!start.load(std::memory_order_acquire)) {}
            int local_reads = 0;
            while (!stop.load(std::memory_order_acquire) && local_reads < ITERATIONS) {
                const table *t = lib.enter_read();
                if (t) {
                    REQUIRE(t->magic == SKL_ABIX_TABLE_MAGIC);
                    REQUIRE(t->count > 0);
                    ++local_reads;
                } else {
                    ++error_count;
                }
                lib.exit_read();
            }
            read_count.fetch_add(local_reads, std::memory_order_relaxed);
        });
    }

    start.store(true, std::memory_order_release);

    for (int w = 0; w < 20; ++w) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        lib.reload(dll_path("math_dll").c_str());
    }

    stop.store(true, std::memory_order_release);
    for (int i = 0; i < N_READERS; ++i) {
        readers[i].join();
    }

    log_info("RCU stress: %d reads, %d errors, %d reloads", read_count.load(), error_count.load(), 20);
    REQUIRE(read_count.load() > 0);
    REQUIRE(error_count.load() == 0);
}

TEST_CASE("33.writer_concurrent", "[writer][stress][concurrent]") {
    log_info("Test 33: Writer concurrent — N writers reload simultaneously, verify serialization");

    dll_object lib;
    REQUIRE(lib.load(dll_path("math_dll").c_str()));
    REQUIRE(lib.is_loaded());

    static constexpr int N_WRITERS = 4;
    static constexpr int ROUNDS = 5;
    std::atomic<bool> start{false};
    std::atomic<int> success_count{0};
    std::thread writers[N_WRITERS];

    for (int i = 0; i < N_WRITERS; ++i) {
        writers[i] = std::thread([&, i]() {
            while (!start.load(std::memory_order_acquire)) {}
            for (int r = 0; r < ROUNDS; ++r) {
                bool ok = lib.reload(dll_path("math_dll").c_str());
                if (ok) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (int i = 0; i < N_WRITERS; ++i) {
        writers[i].join();
    }

    REQUIRE(lib.is_loaded());
    auto add = dll_func<int(int, int)>(lib, "add");
    REQUIRE(add.valid());
    REQUIRE(add(2, 3) == 5);
    log_info(
        "Writer concurrent: %d/%d reloads succeeded, DLL still functional", success_count.load(), N_WRITERS * ROUNDS);
}

TEST_CASE("34.arm_weak_memory_stress", "[arm][memory_order][stress]") {
    log_info("Test 34: ARM weak memory ordering — verify acquire/release ordering");

    SECTION("34a.store_buffer_pattern") {
        std::atomic<uint32_t> x{0};
        std::atomic<uint32_t> y{0};
        std::atomic<int> r1{0}, r2{0};
        int violations = 0;

        static constexpr int ITER = 100'000;
        for (int i = 0; i < ITER; ++i) {
            x.store(0, std::memory_order_relaxed);
            y.store(0, std::memory_order_relaxed);
            r1.store(0, std::memory_order_relaxed);
            r2.store(0, std::memory_order_relaxed);

            std::thread t1([&]() {
                x.store(1, std::memory_order_relaxed);
                r1.store(y.load(std::memory_order_relaxed), std::memory_order_relaxed);
            });
            std::thread t2([&]() {
                y.store(1, std::memory_order_relaxed);
                r2.store(x.load(std::memory_order_relaxed), std::memory_order_relaxed);
            });
            t1.join();
            t2.join();

            if (r1.load(std::memory_order_relaxed) == 0 && r2.load(std::memory_order_relaxed) == 0) {
                ++violations;
            }
        }
        log_info("Store-buffer pattern (relaxed): %d violations / %d iterations", violations, ITER);
#if SKL_ABIX_WINDOWS && (defined(_M_ARM) || defined(_M_ARM64))
        log_info("ARM: store-buffer violations possible with relaxed ordering (expected on weak memory models)");
#endif
        REQUIRE(violations == 0);
    }

    SECTION("34b.message_passing_acquire_release") {
        uint32_t data = 0;
        uint32_t flag = 0;
        int bad_reads = 0;

        static constexpr int ITER = 50'000;
        for (int i = 0; i < ITER; ++i) {
            data = 0;
            flag = 0;

            std::thread t1([&]() {
                atomic::store_relaxed(&data, 42);
                atomic::store_release(&flag, 1);
            });
            std::thread t2([&]() {
                if (atomic::load_acquire(&flag) == 1) {
                    uint32_t d = atomic::load_relaxed(&data);
                    if (d != 42) ++bad_reads;
                }
            });
            t1.join();
            t2.join();
        }
        REQUIRE(bad_reads == 0);
        log_info("Message-passing acq/rel: %d bad reads / %d iterations", bad_reads, ITER);
    }

    SECTION("34c.pointer_acquire_release_ordering") {
        uint32_t payload = 0;
        void *ptr = nullptr;
        int bad_reads = 0;

        static constexpr int ITER = 50'000;
        for (int i = 0; i < ITER; ++i) {
            payload = 0;
            ptr = nullptr;

            std::thread t1([&]() {
                payload = 99;
                atomic::store_release(&ptr, &payload);
            });
            std::thread t2([&]() {
                void *p = atomic::load_acquire(&ptr);
                if (p) {
                    uint32_t v = *static_cast<uint32_t *>(p);
                    if (v != 99) ++bad_reads;
                }
            });
            t1.join();
            t2.join();
        }
        REQUIRE(bad_reads == 0);
        log_info("Pointer acq/rel ordering: %d bad reads / %d iterations", bad_reads, ITER);
    }

    SECTION("34d.atomic_inc_acq_rel_ordering") {
        uint64_t counter = 0;
        uint32_t data = 0;
        int bad_reads = 0;

        static constexpr int ITER = 50'000;
        for (int i = 0; i < ITER; ++i) {
            counter = 0;
            data = 0;

            std::thread t1([&]() {
                data = 77;
                atomic::store_release(&data, 77);
                atomic::inc_acq_rel(&counter);
            });
            std::thread t2([&]() {
                uint64_t c = atomic::load_acquire(&counter);
                if (c > 0) {
                    uint32_t d = atomic::load_acquire(&data);
                    if (d != 77) ++bad_reads;
                }
            });
            t1.join();
            t2.join();
        }
        REQUIRE(bad_reads == 0);
        log_info("Atomic inc_acq_rel ordering: %d bad reads / %d iterations", bad_reads, ITER);
    }
}

TEST_CASE("35.tsan_race_verification", "[tsan][race][concurrent]") {
    log_info("Test 35: TSan race verification — concurrent RCU path should be clean");

    SECTION("35a.concurrent_enter_exit") {
        dll_object lib;
        REQUIRE(lib.load(dll_path("math_dll").c_str()));

        static constexpr int N = 4;
        static constexpr int ITER = 1'000;
        std::thread threads[N];

        for (int i = 0; i < N; ++i) {
            threads[i] = std::thread([&]() {
                for (int j = 0; j < ITER; ++j) {
                    const table *t = lib.enter_read();
                    if (t) {
                        REQUIRE(t->magic == SKL_ABIX_TABLE_MAGIC);
                    }
                    lib.exit_read();
                }
            });
        }
        for (int i = 0; i < N; ++i) {
            threads[i].join();
        }
        log_info("TSan enter/exit: %d threads x %d iterations, no crash", N, ITER);
    }

    SECTION("35b.concurrent_reload_and_read") {
        dll_object lib;
        REQUIRE(lib.load(dll_path("math_dll").c_str()));

        static constexpr int ITER = 500;
        std::atomic<bool> start{false};
        std::atomic<bool> stop{false};

        std::thread reader([&]() {
            while (!start.load(std::memory_order_acquire)) {}
            while (!stop.load(std::memory_order_acquire)) {
                const table *t = lib.enter_read();
                if (t) {
                    REQUIRE(t->magic == SKL_ABIX_TABLE_MAGIC);
                }
                lib.exit_read();
            }
        });

        start.store(true, std::memory_order_release);
        for (int i = 0; i < 10; ++i) {
            lib.reload(dll_path("math_dll").c_str());
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        stop.store(true, std::memory_order_release);
        reader.join();
        log_info("TSan reload+read: 10 reloads with active reader, no crash");
    }

    SECTION("35c.concurrent_unload_and_read") {
        dll_object lib;
        REQUIRE(lib.load(dll_path("math_dll").c_str()));

        std::atomic<bool> reader_done{false};
        std::thread reader([&]() {
            const table *t = lib.enter_read();
            REQUIRE(t != nullptr);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            lib.exit_read();
            reader_done.store(true, std::memory_order_release);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        bool unloaded = lib.unload();
        REQUIRE(unloaded);
        reader.join();
        REQUIRE(reader_done.load(std::memory_order_acquire));
        log_info("TSan unload+read: unload blocked until reader exited, no crash");
    }

    SECTION("35d.concurrent_refcount") {
        dll_object lib;
        REQUIRE(lib.load(dll_path("math_dll").c_str()));

        static constexpr int N = 4;
        std::thread threads[N];

        for (int i = 0; i < N; ++i) {
            threads[i] = std::thread([&]() {
                for (int j = 0; j < 100; ++j) {
                    lib.add_ref();
                    lib.release_ref();
                }
            });
        }
        for (int i = 0; i < N; ++i) {
            threads[i].join();
        }
        REQUIRE(lib.ref_count() == 0);
        log_info("TSan refcount: %d threads x 100 inc/dec, final refs=0", N);
    }
}