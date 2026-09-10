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

// ============================================================
// Protected load (enter + load + exit)
// ============================================================

static void BM_EBR_ProtectedLoad(benchmark::State &state) {
    int payload = 42;
    void *ptr = &payload;
    for (auto _ : state) {
        domain.enter();
        void *p = skl::abix::atomic::load_acquire(&ptr);
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
        void *p = skl::abix::atomic::load_acquire(&ptr);
        benchmark::DoNotOptimize(p);
    }
}
BENCHMARK(BM_Atomic_LoadPointer_Raw);
#endif // SKL_ABIX_DEVELOPMENT