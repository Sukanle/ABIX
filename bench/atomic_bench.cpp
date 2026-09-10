#include <benchmark/benchmark.h>
#include <atomic>

#include "abix/abix.hpp"

// --- u32 load baseline ---
static uint32_t g_raw_load_u32 = 42;
static std::atomic<uint32_t> g_std_load_u32{42};
static uint32_t g_abix_load_u32 = 42;

static void BM_Raw_Load_U32(benchmark::State &state) {
    for (auto _ : state) {
        uint32_t r = g_raw_load_u32;
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Raw_Load_U32);

static void BM_StdAtomic_Load_U32(benchmark::State &state) {
    for (auto _ : state) {
        uint32_t r = g_std_load_u32.load(std::memory_order_acquire);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_StdAtomic_Load_U32);

static void BM_ABIX_Atomic_Load_U32(benchmark::State &state) {
    for (auto _ : state) {
        uint32_t r = skl::abix::atomic::load_acquire(&g_abix_load_u32);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_ABIX_Atomic_Load_U32);

// --- u32 store baseline ---
static uint32_t g_raw_store_u32 = 0;
static std::atomic<uint32_t> g_std_store_u32{0};
static uint32_t g_abix_store_u32 = 0;

static void BM_Raw_Store_U32(benchmark::State &state) {
    for (auto _ : state) {
        g_raw_store_u32 = 42;
        benchmark::DoNotOptimize(g_raw_store_u32);
    }
}
BENCHMARK(BM_Raw_Store_U32);

static void BM_StdAtomic_Store_U32(benchmark::State &state) {
    for (auto _ : state) {
        g_std_store_u32.store(42, std::memory_order_release);
        benchmark::DoNotOptimize(g_std_store_u32);
    }
}
BENCHMARK(BM_StdAtomic_Store_U32);

static void BM_ABIX_Atomic_Store_U32(benchmark::State &state) {
    for (auto _ : state) {
        skl::abix::atomic::store_release(&g_abix_store_u32, 42U);
        benchmark::DoNotOptimize(g_abix_store_u32);
    }
}
BENCHMARK(BM_ABIX_Atomic_Store_U32);

// --- u32 inc baseline ---
static uint32_t g_raw_inc_u32 = 0;
static std::atomic<uint32_t> g_std_inc_u32{0};
static uint32_t g_abix_inc_u32 = 0;

static void BM_Raw_Inc_U32(benchmark::State &state) {
    for (auto _ : state) {
        ++g_raw_inc_u32;
        benchmark::DoNotOptimize(g_raw_inc_u32);
    }
    g_raw_inc_u32 = 0;
}
BENCHMARK(BM_Raw_Inc_U32);

static void BM_StdAtomic_Inc_U32(benchmark::State &state) {
    for (auto _ : state) {
        uint32_t r = g_std_inc_u32.fetch_add(1, std::memory_order_relaxed);
        benchmark::DoNotOptimize(r);
    }
    g_std_inc_u32.store(0, std::memory_order_relaxed);
}
BENCHMARK(BM_StdAtomic_Inc_U32);

static void BM_ABIX_Atomic_Inc_U32(benchmark::State &state) {
    for (auto _ : state) {
        uint32_t r = skl::abix::atomic::inc_relaxed(&g_abix_inc_u32);
        benchmark::DoNotOptimize(r);
    }
    skl::abix::atomic::store_relaxed(&g_abix_inc_u32, 0U);
}
BENCHMARK(BM_ABIX_Atomic_Inc_U32);

// --- u32 load (various memory orders) ---
static void BM_Atomic_Load_U32_Acquire(benchmark::State &state) {
    uint32_t v = 42;
    for (auto _ : state) {
        uint32_t r = skl::abix::atomic::load_acquire(&v);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Atomic_Load_U32_Acquire);

static void BM_Atomic_Load_U32_Relaxed(benchmark::State &state) {
    uint32_t v = 42;
    for (auto _ : state) {
        uint32_t r = skl::abix::atomic::load_relaxed(&v);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Atomic_Load_U32_Relaxed);

// --- u32 store (various memory orders) ---
static void BM_Atomic_Store_U32_Release(benchmark::State &state) {
    uint32_t v = 0;
    for (auto _ : state) {
        skl::abix::atomic::store_release(&v, 42U);
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_Atomic_Store_U32_Release);

static void BM_Atomic_Store_U32_Relaxed(benchmark::State &state) {
    uint32_t v = 0;
    for (auto _ : state) {
        skl::abix::atomic::store_relaxed(&v, 42U);
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_Atomic_Store_U32_Relaxed);

// --- u32 RMW ---
static void BM_Atomic_Inc_U32(benchmark::State &state) {
    uint32_t v = 0;
    for (auto _ : state) {
        uint32_t r = skl::abix::atomic::inc_relaxed(&v);
        benchmark::DoNotOptimize(r);
    }
    skl::abix::atomic::store_relaxed(&v, 0U);
}
BENCHMARK(BM_Atomic_Inc_U32);

static void BM_Atomic_Dec_U32(benchmark::State &state) {
    uint32_t v = 100'000'000;
    for (auto _ : state) {
        uint32_t r = skl::abix::atomic::dec_relaxed(&v);
        benchmark::DoNotOptimize(r);
    }
    skl::abix::atomic::store_relaxed(&v, 100'000'000U);
}
BENCHMARK(BM_Atomic_Dec_U32);

static void BM_Atomic_CAS_U32(benchmark::State &state) {
    uint32_t v = 0;
    for (auto _ : state) {
        bool ok = skl::abix::atomic::cas_relaxed(&v, 0U, 1U);
        benchmark::DoNotOptimize(ok);
        skl::abix::atomic::store_relaxed(&v, 0U);
    }
}
BENCHMARK(BM_Atomic_CAS_U32);

// --- u64 load/store ---
static void BM_Atomic_Load_U64_Acquire(benchmark::State &state) {
    uint64_t v = 42;
    for (auto _ : state) {
        uint64_t r = skl::abix::atomic::load_acquire(&v);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Atomic_Load_U64_Acquire);

static void BM_Atomic_Store_U64_Release(benchmark::State &state) {
    uint64_t v = 0;
    for (auto _ : state) {
        skl::abix::atomic::store_release(&v, 42ULL);
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_Atomic_Store_U64_Release);

static void BM_Atomic_Inc_AcqRel_U64(benchmark::State &state) {
    uint64_t v = 0;
    for (auto _ : state) {
        uint64_t r = skl::abix::atomic::inc_acq_rel(&v);
        benchmark::DoNotOptimize(r);
    }
    skl::abix::atomic::store_release(&v, 0ULL);
}
BENCHMARK(BM_Atomic_Inc_AcqRel_U64);

// --- pointer load/store ---
static void BM_Atomic_Load_Pointer_Acquire(benchmark::State &state) {
    int payload = 42;
    void *p = &payload;
    for (auto _ : state) {
        void *r = skl::abix::atomic::load_acquire(&p);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Atomic_Load_Pointer_Acquire);

static void BM_Atomic_Store_Pointer_Release(benchmark::State &state) {
    int payload = 42;
    void *p = nullptr;
    for (auto _ : state) {
        skl::abix::atomic::store_release(&p, &payload);
        benchmark::DoNotOptimize(p);
        p = nullptr;
    }
}
BENCHMARK(BM_Atomic_Store_Pointer_Release);

static void BM_Atomic_IncShared(benchmark::State &state) {
    static uint64_t g_shared_counter = 0;
    for (auto _ : state) {
        uint64_t r = skl::abix::atomic::inc_acq_rel(&g_shared_counter);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Atomic_IncShared)->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16);

static void BM_Atomic_PerThread_Inc(benchmark::State &state) {
    static uint32_t g_slots[64] = {};
    int tid = state.thread_index() % 64;
    for (auto _ : state) {
        uint32_t r = skl::abix::atomic::inc_relaxed(&g_slots[tid]);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Atomic_PerThread_Inc)->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16);

struct alignas(128) PaddedCounter {
    uint32_t value;
};

static void BM_FalseSharing_Adjacent(benchmark::State &state) {
    static uint32_t g_adjacent[64] = {};
    int tid = state.thread_index() % 64;
    for (auto _ : state) {
        skl::abix::atomic::inc_relaxed(&g_adjacent[tid]);
    }
}
BENCHMARK(BM_FalseSharing_Adjacent)->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16);

static void BM_FalseSharing_Padded(benchmark::State &state) {
    static PaddedCounter g_padded[64] = {};
    int tid = state.thread_index() % 64;
    for (auto _ : state) {
        skl::abix::atomic::inc_relaxed(&g_padded[tid].value);
    }
}
BENCHMARK(BM_FalseSharing_Padded)->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16);