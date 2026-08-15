#include <benchmark/benchmark.h>

#include "abix/abix.hpp"

using namespace skl::abix;

static rcu_domain &domain = rcu_domain::instance();

static void BM_EBR_Enter(benchmark::State &state) {
    for (auto _ : state) {
        domain.enter();
        benchmark::DoNotOptimize(domain);
        domain.exit();
    }
}
BENCHMARK(BM_EBR_Enter);

static void BM_EBR_Exit(benchmark::State &state) {
    for (auto _ : state) {
        domain.enter();
        benchmark::DoNotOptimize(domain);
        domain.exit();
    }
}
BENCHMARK(BM_EBR_Exit);

static void BM_EBR_EnterExit(benchmark::State &state) {
    for (auto _ : state) {
        domain.enter();
        domain.exit();
    }
}
BENCHMARK(BM_EBR_EnterExit);

static void BM_EBR_ProtectedLoad(benchmark::State &state) {
    int payload = 42;
    void *ptr = &payload;
    for (auto _ : state) {
        domain.enter();
        void *p = atomic::load_acquire(&ptr);
        benchmark::DoNotOptimize(p);
        domain.exit();
    }
}
BENCHMARK(BM_EBR_ProtectedLoad);

static void BM_Atomic_LoadPointer_Raw(benchmark::State &state) {
    int payload = 42;
    void *ptr = &payload;
    for (auto _ : state) {
        void *p = atomic::load_acquire(&ptr);
        benchmark::DoNotOptimize(p);
    }
}
BENCHMARK(BM_Atomic_LoadPointer_Raw);

static void BM_EBR_Guard_RAII(benchmark::State &state) {
    for (auto _ : state) {
        rcu_guard guard(domain);
        benchmark::DoNotOptimize(guard);
    }
}
BENCHMARK(BM_EBR_Guard_RAII);