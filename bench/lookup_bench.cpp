#include <benchmark/benchmark.h>
#include <stdio.h>

#include "bench_common.hpp"

// Shared state: loaded once before benchmarks
static skl::abix::dll_object *g_lib = nullptr;
static const skl::abix::table *g_table = nullptr;
static skl::abix::dll_image *g_image = nullptr;

static void setup_lookup_bench() {
    if (g_lib) return;
    g_lib = new skl::abix::dll_object();
    g_lib->load(dll_path("hotcache_dll").c_str());
    g_table = g_lib->get_table();
    g_image = g_lib->image_acquire();
}

static void teardown_lookup_bench() {
    if (g_lib) {
        delete g_lib;
        g_lib = nullptr;
        g_table = nullptr;
    }
}

// --- BM_FindIndex_Only (baseline) ---
static void BM_FindIndex_Only(benchmark::State &state) {
    setup_lookup_bench();
    skl::abix::sig_t sg = skl::abix::fn_sig<int(int)>::value;
    for (auto _ : state) {
        const char *nm = hot_name(state.iterations() % 20);
        skl::abix::index_t idx;
        find_index(*g_table, g_image->index, nm, sg, 0, idx);
        benchmark::DoNotOptimize(idx);
    }
}
BENCHMARK(BM_FindIndex_Only);

// --- BM_Resolve_Linear ---
static void BM_Resolve_Linear(benchmark::State &state) {
    setup_lookup_bench();
    skl::abix::sig_t sg = skl::abix::fn_sig<int(int)>::value;
    for (auto _ : state) {
        const char *nm = hot_name(state.iterations() % 20);
        const skl::abix::entry *e = lookup_linear(*g_table, nm, Reflect::Utils::cstr32(nm), sg);
        benchmark::DoNotOptimize(e);
    }
}
BENCHMARK(BM_Resolve_Linear);

// --- BM_Resolve_WithEBR ---
static void BM_Resolve_WithEBR(benchmark::State &state) {
    setup_lookup_bench();
    skl::abix::rcu_domain &dom = skl::abix::rcu_domain::instance();
    skl::abix::sig_t sg = skl::abix::fn_sig<int(int)>::value;
    for (auto _ : state) {
        dom.enter();
        skl::abix::dll_image *img = g_lib->image_acquire();
        const char *nm = hot_name(state.iterations() % 20);
        skl::abix::index_t idx;
        if (img && img->table) {
            find_index(*img->table, img->index, nm, sg, 0, idx);
        }
        benchmark::DoNotOptimize(idx);
        dom.exit();
    }
}
BENCHMARK(BM_Resolve_WithEBR);

// --- 80/20 distribution ---
static void BM_Resolve_Linear_80_20(benchmark::State &state) {
    setup_lookup_bench();
    skl::abix::sig_t sg = skl::abix::fn_sig<int(int)>::value;
    for (auto _ : state) {
        int i = (int)state.iterations();
        const char *nm = (i % 100 < 80) ? hot_name(i % 20) : cold_name(i % 1'000);
        const skl::abix::entry *e = lookup_linear(*g_table, nm, Reflect::Utils::cstr32(nm), sg);
        benchmark::DoNotOptimize(e);
    }
}
BENCHMARK(BM_Resolve_Linear_80_20);

// --- Random access (no hot entries) ---
static void BM_Resolve_Linear_Random(benchmark::State &state) {
    setup_lookup_bench();
    skl::abix::sig_t sg = skl::abix::fn_sig<int(int)>::value;
    for (auto _ : state) {
        const char *nm = cold_name((int)state.iterations() % 1'000);
        const skl::abix::entry *e = lookup_linear(*g_table, nm, Reflect::Utils::cstr32(nm), sg);
        benchmark::DoNotOptimize(e);
    }
}
BENCHMARK(BM_Resolve_Linear_Random);

#ifndef ALL_BENCHMARKS
int main(int argc, char **argv) {
    skl::abix::rcu_domain::instance().reset();

    benchmark::MaybeReenterWithoutASLR(argc, argv);
    char arg0_default[] = "benchmark";
    char *args_default = reinterpret_cast<char *>(arg0_default);
    if (!argv) {
        argc = 1;
        argv = &args_default;
    }
    ::benchmark::Initialize(&argc, argv);
    if (::benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    ::benchmark::RunSpecifiedBenchmarks();
    ::benchmark::Shutdown();
    return 0;
}
#endif