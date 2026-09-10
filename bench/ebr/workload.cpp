// ============================================================
// Workload — Mixed reader/writer EBR benchmarks (development-only)
// ============================================================

#include "ebr_common.hpp"

#ifdef SKL_ABIX_DEVELOPMENT

// ============================================================
// RWPhase: mixed reader/writer workload
//
// writer_count threads retire + synchronize, the rest read.
// ============================================================

static void BM_EBR_RWPhase(benchmark::State &state) {
    int writer_count = state.range(0);
    int tid = state.thread_index();
    bool is_writer = (tid < writer_count);

    domain.synchronize();

    for (auto _ : state) {
        if (is_writer) {
            int *p = new int(42);
            benchmark::DoNotOptimize(p);
            domain.retire(p, [](void *obj) noexcept { delete static_cast<int *>(obj); });
            domain.synchronize();
        } else {
            domain.enter();
            domain.exit();
        }
    }

    domain.synchronize();
}
BENCHMARK(BM_EBR_RWPhase)
    ->Args({1})->Args({2})->Args({4})->Args({8})
    ->Threads(2)->Threads(4)->Threads(8)->Threads(16)->Threads(32);

#endif // SKL_ABIX_DEVELOPMENT