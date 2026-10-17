// ============================================================
// Fast Path — Reader-side EBR microbenchmarks
//
// Kept: EnterExit, ProtectedLoad, Atomic_LoadPointer_Raw.
//       Reader scalability (BM_EBR_ReaderPhase) is registered
//       by ebr_profile_bench with topology-aware thread counts.
//       Guard_RAII is redundant with EnterExit.
// ============================================================

#include "ebr_common.hpp"

// ============================================================
// Pure enter/exit
// ============================================================

static void BM_EBR_EnterExit(benchmark::State &state) {
    for (auto _ : state) {
        domain.enter();
        domain.exit();
    }
}
BENCHMARK(BM_EBR_EnterExit);

// Probe: the same loop under contention. `ThreadState` (the per-thread slot
// `enter()`/`exit()` write) is 16 bytes and separately `new`-ed, so two threads'
// slots can share a cache line and ping-pong on every enter/exit. These thread
// variants make that visible: a flat per-op cost means no sharing, a rising one
// means the slots are colliding.
static void BM_EBR_EnterExit_Threads(benchmark::State &state) {
    for (auto _ : state) {
        domain.enter();
        domain.exit();
    }
}
BENCHMARK(BM_EBR_EnterExit_Threads)->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16);

// ============================================================
// Protected load (enter + load + exit)
// ============================================================

static void BM_EBR_ProtectedLoad(benchmark::State &state) {
    int payload = 42;
    void *ptr = &payload;
    for (auto _ : state) {
        domain.enter();
        void *p = skl::abix::util::load_acquire(&ptr);
        benchmark::DoNotOptimize(p);
        domain.exit();
    }
}
BENCHMARK(BM_EBR_ProtectedLoad);

#ifdef SKL_ABIX_DEVELOPMENT
// ============================================================
// Reference: raw atomic load (no EBR)
// ============================================================

static void BM_Atomic_LoadPointer_Raw(benchmark::State &state) {
    int payload = 42;
    void *ptr = &payload;
    for (auto _ : state) {
        void *p = skl::abix::util::load_acquire(&ptr);
        benchmark::DoNotOptimize(p);
    }
}
BENCHMARK(BM_Atomic_LoadPointer_Raw);
#endif // SKL_ABIX_DEVELOPMENT
