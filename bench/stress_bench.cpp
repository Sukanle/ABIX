#include <benchmark/benchmark.h>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

#include "abix/abix.hpp"

using namespace skl::abix;

inline void busy_wait_ns(uint64_t ns) {
    auto start = std::chrono::high_resolution_clock::now();
    while (true) {
        auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - start)
                .count();
        if ((uint64_t)elapsed >= ns) break;
    }
}

struct alignas(64) LatencyStats {
    uint64_t count = 0;
    uint64_t total_ns = 0;
    uint64_t min_ns = UINT64_MAX;
    uint64_t max_ns = 0;
    uint64_t buckets[32] = {};

    void record(uint64_t ns) {
        count++;
        total_ns += ns;
        if (ns < min_ns) min_ns = ns;
        if (ns > max_ns) max_ns = ns;
        int b = 0;
        if (ns > 0) {
            unsigned long idx;
            _BitScanReverse64(&idx, ns);
            b = (int)idx;
        }
        if (b >= 32) b = 31;
        buckets[b]++;
    }

    uint64_t percentile(double p) const {
        if (count == 0) return 0;
        uint64_t target = (uint64_t)((double)count * p);
        if (target >= count) target = count - 1;
        uint64_t cum = 0;
        for (int i = 0; i < 32; ++i) {
            cum += buckets[i];
            if (cum > target) return 1ULL << i;
        }
        return max_ns;
    }

    uint64_t avg_ns() const { return count ? total_ns / count : 0; }
};

static void report_latency(benchmark::State &state, const LatencyStats &s, const char *prefix) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s_avg", prefix);
    state.counters[buf] = (double)s.avg_ns();
    snprintf(buf, sizeof(buf), "%s_p50", prefix);
    state.counters[buf] = (double)s.percentile(0.50);
    snprintf(buf, sizeof(buf), "%s_p95", prefix);
    state.counters[buf] = (double)s.percentile(0.95);
    snprintf(buf, sizeof(buf), "%s_p99", prefix);
    state.counters[buf] = (double)s.percentile(0.99);
    snprintf(buf, sizeof(buf), "%s_max", prefix);
    state.counters[buf] = (double)s.max_ns;
    snprintf(buf, sizeof(buf), "%s_ops", prefix);
    state.counters[buf] = (double)s.count;
}

static void BM_EBR_ReaderScalability(benchmark::State &state) {
    rcu_domain &dom = rcu_domain::instance();
    uint64_t ops = 0;
    for (auto _ : state) {
        dom.enter();
        dom.exit();
        ++ops;
    }
    state.counters["reader_ops"] = (double)ops;
}
BENCHMARK(BM_EBR_ReaderScalability)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->Threads(16)
    ->Threads(20)
    ->Threads(32);

static void BM_EBR_WriterScalability(benchmark::State &state) {
    const int readers = (int)state.range(0);
    const int writers = (int)state.range(1);
    rcu_domain &dom = rcu_domain::instance();
    std::atomic<bool> running{true};
    std::vector<std::thread> workers;

    for (int i = 0; i < readers; ++i) {
        workers.emplace_back([&]() {
            while (running.load(std::memory_order_acquire)) {
                dom.enter();
                dom.exit();
            }
        });
    }

    for (auto _ : state) {
        dom.synchronize();
    }

    running.store(false, std::memory_order_release);
    for (auto &w : workers)
        w.join();
}
BENCHMARK(BM_EBR_WriterScalability)
    ->Args({0, 1})
    ->Args({1, 1})
    ->Args({4, 1})
    ->Args({8, 1})
    ->Args({16, 1})
    ->Args({0, 2})
    ->Args({4, 2})
    ->Args({8, 2})
    ->Args({16, 2})
    ->Args({0, 4})
    ->Args({4, 4})
    ->Args({8, 4})
    ->Args({16, 4});

static void BM_EBR_ReadWriteRatio(benchmark::State &state) {
    const int readers = (int)state.range(0);
    const int writers = (int)state.range(1);
    rcu_domain &dom = rcu_domain::instance();
    std::atomic<bool> running{true};
    std::atomic<bool> start{false};
    std::atomic<uint64_t> ready{0};

    std::vector<LatencyStats> r_stats(readers);
    std::vector<LatencyStats> w_stats(writers);
    std::vector<std::thread> workers;

    for (int i = 0; i < readers; ++i) {
        workers.emplace_back([&, i]() {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {}
            auto t0 = std::chrono::high_resolution_clock::now();
            while (running.load(std::memory_order_acquire)) {
                auto t1 = std::chrono::high_resolution_clock::now();
                dom.enter();
                dom.exit();
                auto t2 = std::chrono::high_resolution_clock::now();
                r_stats[i].record((uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count());
            }
            benchmark::DoNotOptimize(t0);
        });
    }

    for (int i = 0; i < writers; ++i) {
        workers.emplace_back([&, i]() {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {}
            while (running.load(std::memory_order_acquire)) {
                auto t1 = std::chrono::high_resolution_clock::now();
                dom.synchronize();
                auto t2 = std::chrono::high_resolution_clock::now();
                w_stats[i].record((uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count());
            }
        });
    }

    while (ready.load(std::memory_order_acquire) < (uint64_t)(readers + writers)) {}
    start.store(true, std::memory_order_release);

    for (auto _ : state) {
        dom.enter();
        dom.exit();
    }

    running.store(false, std::memory_order_release);
    for (auto &w : workers)
        w.join();

    LatencyStats r_total, w_total;
    for (auto &s : r_stats) {
        r_total.count += s.count;
        r_total.total_ns += s.total_ns;
        if (s.min_ns < r_total.min_ns) r_total.min_ns = s.min_ns;
        if (s.max_ns > r_total.max_ns) r_total.max_ns = s.max_ns;
        for (int b = 0; b < 32; ++b)
            r_total.buckets[b] += s.buckets[b];
    }
    for (auto &s : w_stats) {
        w_total.count += s.count;
        w_total.total_ns += s.total_ns;
        if (s.min_ns < w_total.min_ns) w_total.min_ns = s.min_ns;
        if (s.max_ns > w_total.max_ns) w_total.max_ns = s.max_ns;
        for (int b = 0; b < 32; ++b)
            w_total.buckets[b] += s.buckets[b];
    }

    report_latency(state, r_total, "reader");
    report_latency(state, w_total, "writer");
}
BENCHMARK(BM_EBR_ReadWriteRatio)
    ->Args({1, 1})
    ->Args({4, 1})
    ->Args({8, 1})
    ->Args({16, 1})
    ->Args({32, 1})
    ->Args({4, 2})
    ->Args({8, 2})
    ->Args({16, 2})
    ->Args({4, 4})
    ->Args({8, 4})
    ->Args({16, 4});

static void BM_EBR_GracePeriod(benchmark::State &state) {
    const uint64_t reader_ns = (uint64_t)state.range(0);
    rcu_domain &dom = rcu_domain::instance();
    std::atomic<bool> running{true};
    std::atomic<bool> reader_inside{false};
    std::atomic<uint64_t> sync_latency_ns{0};
    std::atomic<uint64_t> sync_count{0};

    std::thread reader([&]() {
        while (running.load(std::memory_order_acquire)) {
            dom.enter();
            reader_inside.store(true, std::memory_order_release);
            busy_wait_ns(reader_ns);
            reader_inside.store(false, std::memory_order_release);
            dom.exit();
        }
    });

    while (!reader_inside.load(std::memory_order_acquire)) {}

    for (auto _ : state) {
        auto t1 = std::chrono::high_resolution_clock::now();
        dom.synchronize();
        auto t2 = std::chrono::high_resolution_clock::now();
        sync_latency_ns.fetch_add(
            (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count(), std::memory_order_relaxed);
        sync_count.fetch_add(1, std::memory_order_relaxed);
    }

    running.store(false, std::memory_order_release);
    reader.join();

    uint64_t count = sync_count.load();
    state.counters["sync_avg_ns"] = count ? (double)sync_latency_ns.load() / (double)count : 0.0;
    state.counters["reader_dur_ns"] = (double)reader_ns;
}
BENCHMARK(BM_EBR_GracePeriod)->Arg(10)->Arg(100)->Arg(1'000)->Arg(10'000)->Arg(100'000)->Arg(1'000'000);

static void BM_EBR_ReclaimBatch(benchmark::State &state) {
    const int batch = (int)state.range(0);
    rcu_domain &dom = rcu_domain::instance();
    for (auto _ : state) {
        for (int i = 0; i < batch; ++i)
            dom.retire(nullptr, [](void *) noexcept {});
        auto t1 = std::chrono::high_resolution_clock::now();
        dom.synchronize();
        auto t2 = std::chrono::high_resolution_clock::now();
        uint64_t ns = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
        state.counters["ns_per_object"] = (double)ns / (double)batch;
    }
}
BENCHMARK(BM_EBR_ReclaimBatch)->Arg(1)->Arg(10)->Arg(100)->Arg(1'000)->Arg(10'000);

static void BM_EBR_MixedWorkload(benchmark::State &state) {
    const int readers = (int)state.range(0);
    const int writer_interval = (int)state.range(1);
    rcu_domain &dom = rcu_domain::instance();
    std::atomic<bool> running{true};
    std::atomic<bool> start{false};
    std::atomic<uint64_t> ready{0};

    int payload = 42;
    void *ptr = &payload;

    std::vector<LatencyStats> r_stats(readers);
    LatencyStats w_stats;
    std::vector<std::thread> workers;

    for (int i = 0; i < readers; ++i) {
        workers.emplace_back([&, i]() {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {}
            while (running.load(std::memory_order_acquire)) {
                auto t1 = std::chrono::high_resolution_clock::now();
                dom.enter();
                void *p = atomic::load_acquire(&ptr);
                benchmark::DoNotOptimize(p);
                dom.exit();
                auto t2 = std::chrono::high_resolution_clock::now();
                r_stats[i].record((uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count());
            }
        });
    }

    while (ready.load(std::memory_order_acquire) < (uint64_t)readers) {}
    start.store(true, std::memory_order_release);

    uint64_t iter = 0;
    for (auto _ : state) {
        dom.enter();
        dom.exit();
        if (++iter % (uint64_t)writer_interval == 0) {
            auto t1 = std::chrono::high_resolution_clock::now();
            dom.synchronize();
            auto t2 = std::chrono::high_resolution_clock::now();
            w_stats.record((uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count());
        }
    }

    running.store(false, std::memory_order_release);
    for (auto &w : workers)
        w.join();

    LatencyStats r_total;
    for (auto &s : r_stats) {
        r_total.count += s.count;
        r_total.total_ns += s.total_ns;
        if (s.min_ns < r_total.min_ns) r_total.min_ns = s.min_ns;
        if (s.max_ns > r_total.max_ns) r_total.max_ns = s.max_ns;
        for (int b = 0; b < 32; ++b)
            r_total.buckets[b] += s.buckets[b];
    }

    report_latency(state, r_total, "reader");
    report_latency(state, w_stats, "writer");
}
BENCHMARK(BM_EBR_MixedWorkload)
    ->Args({1, 100})
    ->Args({4, 100})
    ->Args({8, 100})
    ->Args({16, 100})
    ->Args({4, 10})
    ->Args({8, 10})
    ->Args({16, 10});

static void BM_EBR_ReadWriteOps(benchmark::State &state) {
    const int readers = (int)state.range(0);
    const int writers = (int)state.range(1);
    rcu_domain &dom = rcu_domain::instance();
    std::atomic<bool> running{true};
    std::atomic<uint64_t> reader_ops{0};
    std::atomic<uint64_t> writer_ops{0};
    std::vector<std::thread> workers;

    for (int i = 0; i < readers; ++i) {
        workers.emplace_back([&]() {
            while (running.load(std::memory_order_acquire)) {
                dom.enter();
                dom.exit();
                reader_ops.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (int i = 0; i < writers; ++i) {
        workers.emplace_back([&]() {
            while (running.load(std::memory_order_acquire)) {
                dom.synchronize();
                writer_ops.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto _ : state) {
        dom.enter();
        dom.exit();
    }

    running.store(false, std::memory_order_release);
    for (auto &w : workers)
        w.join();
    state.counters["reader_ops"] = (double)reader_ops.load();
    state.counters["writer_ops"] = (double)writer_ops.load();
}
BENCHMARK(BM_EBR_ReadWriteOps)
    ->Args({1, 1})
    ->Args({4, 1})
    ->Args({8, 1})
    ->Args({16, 1})
    ->Args({32, 1})
    ->Args({4, 2})
    ->Args({8, 2})
    ->Args({16, 2})
    ->Args({4, 4})
    ->Args({8, 4})
    ->Args({16, 4});

static void BM_Atomic_Inc_Contention(benchmark::State &state) {
    static uint32_t v = 0;
    for (auto _ : state) {
        atomic::inc_relaxed(&v);
    }
}
BENCHMARK(BM_Atomic_Inc_Contention)->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16);

static void BM_Atomic_Load_Contention(benchmark::State &state) {
    static uint32_t v = 42;
    for (auto _ : state) {
        uint32_t r = atomic::load_acquire(&v);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Atomic_Load_Contention)->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16);