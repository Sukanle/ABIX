#include <benchmark/benchmark.h>
#include <stdio.h>

#include "bench_common.hpp"

using namespace skl::abix;

// --- BM_EBR_Reload_Logical (simulate image swap + retire + advance epoch + collect) ---
static void BM_EBR_Reload_Logical(benchmark::State &state) {
    std::string path = dll_path("reload_dll_a");
    dll_object lib;
    lib.load(path.c_str());

    for (auto _ : state) {
        lib.reload(dll_path("reload_dll_b").c_str());
        lib.reload(path.c_str());
    }
}
BENCHMARK(BM_EBR_Reload_Logical);

// --- BM_DLL_Real_Reload ---
static void BM_DLL_Real_Reload(benchmark::State &state) {
    std::string path_a = dll_path("reload_dll_a");
    std::string path_b = dll_path("reload_dll_b");
    dll_object lib;
    lib.load(path_a.c_str());

    for (auto _ : state) {
        lib.reload(path_b.c_str());
        lib.reload(path_a.c_str());
    }
}
BENCHMARK(BM_DLL_Real_Reload);

// --- BM_EBR_Synchronize (full epoch advance) ---
static void BM_EBR_Synchronize(benchmark::State &state) {
    rcu_domain &dom = rcu_domain::instance();
    for (auto _ : state) {
        dom.synchronize();
    }
}
BENCHMARK(BM_EBR_Synchronize);

// --- BM_EBR_TryCollect_Empty (try_collect when nothing to reclaim) ---
static void BM_EBR_TryCollect_Empty(benchmark::State &state) {
    rcu_domain &dom = rcu_domain::instance();
    for (auto _ : state) {
        dom.try_collect();
    }
}
BENCHMARK(BM_EBR_TryCollect_Empty);