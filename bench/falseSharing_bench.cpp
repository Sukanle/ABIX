// ============================================================
// False Sharing Experiments
//
// Validates the cache-line alignment design decision for
// rcu_slot.  These benchmarks are NOT part of the EBR
// performance benchmark suite — they are microarchitectural
// evidence supporting the alignas(64) choice.
//
// Key result (M4 reference):
//   WritePacked  16T  2.01 ns  (cache-line ping-pong)
//   WriteAligned 16T  1.51 ns  (no false sharing)
//
// The EBR pattern (N writers + 1 scanner) shows a smaller
// gap because the scanner only reads, not writes, so it
// does not cause cache-line ownership transfer.
// ============================================================

#include <benchmark/benchmark.h>
#include <atomic>
#include <memory>

#include "abix/abix.hpp"

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#elif defined(__linux__)
#include <pthread.h>
#endif

#ifndef SET_THREAD_AFFINITY
#define SET_THREAD_AFFINITY
inline bool set_thread_affinity(int cpu_id) {
#if defined(__APPLE__)
    thread_affinity_policy_data_t policy = {cpu_id};
    thread_port_t thread = pthread_mach_thread_np(pthread_self());
    kern_return_t kr = thread_policy_set(thread, THREAD_AFFINITY_POLICY,
                                         (thread_policy_t)&policy,
                                         THREAD_AFFINITY_POLICY_COUNT);
    return kr == KERN_SUCCESS;
#elif defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
#else
    (void)cpu_id;
    return false;
#endif
}
#endif

struct SlotPacked {
    std::atomic<uint64_t> epoch;
    std::atomic<uint32_t> state;
};

struct alignas(64) SlotAligned {
    std::atomic<uint64_t> epoch;
    std::atomic<uint32_t> state;
};

// ============================================================
// Writer-only: each thread writes its own slot.
// Packed:  adjacent slots share cache lines → ping-pong
// Aligned: each slot has its own cache line → no ping-pong
// ============================================================

static void BM_FalseSharing_WritePacked(benchmark::State &state) {
    int tid = state.thread_index();
    set_thread_affinity(tid);

    static std::unique_ptr<SlotPacked[]> slots{new SlotPacked[16]};

    SlotPacked &s = slots[tid];
    uint64_t counter = 0;

    for (auto _ : state) {
        s.epoch.store(++counter, std::memory_order_relaxed);
        s.state.store(static_cast<uint32_t>(counter & 1), std::memory_order_relaxed);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_FalseSharing_WritePacked)
    ->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16);

static void BM_FalseSharing_WriteAligned(benchmark::State &state) {
    int tid = state.thread_index();
    set_thread_affinity(tid);

    static std::unique_ptr<SlotAligned[]> slots{new SlotAligned[16]};

    SlotAligned &s = slots[tid];
    uint64_t counter = 0;

    for (auto _ : state) {
        s.epoch.store(++counter, std::memory_order_relaxed);
        s.state.store(static_cast<uint32_t>(counter & 1), std::memory_order_relaxed);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_FalseSharing_WriteAligned)
    ->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16);

// ============================================================
// EBR-pattern: N threads write own slot + 1 thread reads all.
// ============================================================

static void BM_FalseSharing_EBRPatternPacked(benchmark::State &state) {
    int tid = state.thread_index();
    int nthreads = state.threads();
    set_thread_affinity(tid);

    static std::unique_ptr<SlotPacked[]> slots{new SlotPacked[16]};

    if (tid == 0) {
        for (auto _ : state) {
            for (int i = 0; i < nthreads; ++i) {
                benchmark::DoNotOptimize(slots[i].epoch.load(std::memory_order_relaxed));
                benchmark::DoNotOptimize(slots[i].state.load(std::memory_order_relaxed));
            }
        }
    } else {
        SlotPacked &s = slots[tid];
        uint64_t counter = 0;

        for (auto _ : state) {
            s.epoch.store(++counter, std::memory_order_relaxed);
            s.state.store(static_cast<uint32_t>(counter & 1), std::memory_order_relaxed);
            benchmark::ClobberMemory();
        }
    }
}
BENCHMARK(BM_FalseSharing_EBRPatternPacked)
    ->Threads(2)->Threads(4)->Threads(8)->Threads(16);

static void BM_FalseSharing_EBRPatternAligned(benchmark::State &state) {
    int tid = state.thread_index();
    int nthreads = state.threads();
    set_thread_affinity(tid);

    static std::unique_ptr<SlotAligned[]> slots{new SlotAligned[16]};

    if (tid == 0) {
        for (auto _ : state) {
            for (int i = 0; i < nthreads; ++i) {
                benchmark::DoNotOptimize(slots[i].epoch.load(std::memory_order_relaxed));
                benchmark::DoNotOptimize(slots[i].state.load(std::memory_order_relaxed));
            }
        }
    } else {
        SlotAligned &s = slots[tid];
        uint64_t counter = 0;

        for (auto _ : state) {
            s.epoch.store(++counter, std::memory_order_relaxed);
            s.state.store(static_cast<uint32_t>(counter & 1), std::memory_order_relaxed);
            benchmark::ClobberMemory();
        }
    }
}
BENCHMARK(BM_FalseSharing_EBRPatternAligned)
    ->Threads(2)->Threads(4)->Threads(8)->Threads(16);