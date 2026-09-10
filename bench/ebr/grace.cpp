// ============================================================
// Grace Period — EBR grace-period cost (development-only)
// ============================================================

#include "ebr_common.hpp"

#ifdef SKL_ABIX_DEVELOPMENT

// ============================================================
// GracePhase: retire N objects + synchronize
// ============================================================

static void BM_EBR_GracePhase(benchmark::State &state) {
    int retire_count = state.range(0);

    domain.synchronize();

    for (auto _ : state) {
        for (int i = 0; i < retire_count; ++i) {
            int *p = new int(42);
            benchmark::DoNotOptimize(p);
            domain.retire(p, [](void *obj) noexcept { delete static_cast<int *>(obj); });
        }
        domain.synchronize();
    }

    domain.synchronize();
}
BENCHMARK(BM_EBR_GracePhase)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000);

#endif // SKL_ABIX_DEVELOPMENT