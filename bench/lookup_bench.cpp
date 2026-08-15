#include <benchmark/benchmark.h>
#include <stdio.h>

#include "bench_common.hpp"

using namespace skl::abix;

// Shared state: loaded once before benchmarks
static dll_object *g_lib = nullptr;
static const table *g_table = nullptr;
static adaptive_hot_cache g_adaptive_cache;
static static_hot_cache g_static_cache;

static void setup_lookup_bench() {
    if (g_lib) return;
    g_lib = new dll_object();
    g_lib->load(dll_path("hotcache_dll").c_str());
    g_table = g_lib->get_table();

    for (int id : HOT_IDS)
        g_static_cache.add(id);

    g_adaptive_cache.init(g_table->count);
    sig_t sg = fn_sig<int(int)>::value;
    for (int i = 0; i < 20'000; ++i) {
        const char *nm = hot_name(i % 20);
        lookup_adaptive(*g_table, g_adaptive_cache, nm, Reflect::Utils::cstr32(nm), sg);
    }
}

static void teardown_lookup_bench() {
    if (g_lib) {
        g_adaptive_cache.destroy();
        delete g_lib;
        g_lib = nullptr;
        g_table = nullptr;
    }
}

// --- BM_FindIndex_Only (baseline) ---
static void BM_FindIndex_Only(benchmark::State &state) {
    setup_lookup_bench();
    sig_t sg = fn_sig<int(int)>::value;
    for (auto _ : state) {
        const char *nm = hot_name(state.iterations() % 20);
        index_t idx;
        find_index(*g_table, nm, sg, 0, idx);
        benchmark::DoNotOptimize(idx);
    }
}
BENCHMARK(BM_FindIndex_Only);

// --- BM_Resolve_Linear ---
static void BM_Resolve_Linear(benchmark::State &state) {
    setup_lookup_bench();
    sig_t sg = fn_sig<int(int)>::value;
    for (auto _ : state) {
        const char *nm = hot_name(state.iterations() % 20);
        const entry *e = lookup_linear(*g_table, nm, Reflect::Utils::cstr32(nm), sg);
        benchmark::DoNotOptimize(e);
    }
}
BENCHMARK(BM_Resolve_Linear);

// --- BM_Resolve_StaticHot ---
static void BM_Resolve_StaticHot(benchmark::State &state) {
    setup_lookup_bench();
    sig_t sg = fn_sig<int(int)>::value;
    for (auto _ : state) {
        const char *nm = hot_name(state.iterations() % 20);
        const entry *e = lookup_static_hot(*g_table, g_static_cache, nm, Reflect::Utils::cstr32(nm), sg);
        benchmark::DoNotOptimize(e);
    }
}
BENCHMARK(BM_Resolve_StaticHot);

// --- BM_Resolve_AdaptiveHot ---
static void BM_Resolve_AdaptiveHot(benchmark::State &state) {
    setup_lookup_bench();
    sig_t sg = fn_sig<int(int)>::value;
    for (auto _ : state) {
        const char *nm = hot_name(state.iterations() % 20);
        const entry *e = lookup_adaptive(*g_table, g_adaptive_cache, nm, Reflect::Utils::cstr32(nm), sg);
        benchmark::DoNotOptimize(e);
    }
}
BENCHMARK(BM_Resolve_AdaptiveHot);

// --- BM_Resolve_WithEBR ---
static void BM_Resolve_WithEBR(benchmark::State &state) {
    setup_lookup_bench();
    rcu_domain &dom = rcu_domain::instance();
    sig_t sg = fn_sig<int(int)>::value;
    for (auto _ : state) {
        dom.enter();
        dll_image *img = g_lib->image_acquire();
        const char *nm = hot_name(state.iterations() % 20);
        index_t idx;
        if (img && img->table) {
            find_index(*img->table, nm, sg, 0, idx);
        }
        benchmark::DoNotOptimize(idx);
        dom.exit();
    }
}
BENCHMARK(BM_Resolve_WithEBR);

// --- 80/20 distribution ---
static void BM_Resolve_Linear_80_20(benchmark::State &state) {
    setup_lookup_bench();
    sig_t sg = fn_sig<int(int)>::value;
    for (auto _ : state) {
        int i = (int)state.iterations();
        const char *nm = (i % 100 < 80) ? hot_name(i % 20) : cold_name(i % 1'000);
        const entry *e = lookup_linear(*g_table, nm, Reflect::Utils::cstr32(nm), sg);
        benchmark::DoNotOptimize(e);
    }
}
BENCHMARK(BM_Resolve_Linear_80_20);

static void BM_Resolve_AdaptiveHot_80_20(benchmark::State &state) {
    setup_lookup_bench();
    sig_t sg = fn_sig<int(int)>::value;
    for (auto _ : state) {
        int i = (int)state.iterations();
        const char *nm = (i % 100 < 80) ? hot_name(i % 20) : cold_name(i % 1'000);
        const entry *e = lookup_adaptive(*g_table, g_adaptive_cache, nm, Reflect::Utils::cstr32(nm), sg);
        benchmark::DoNotOptimize(e);
    }
}
BENCHMARK(BM_Resolve_AdaptiveHot_80_20);

// --- Cold start ---
static void BM_Resolve_AdaptiveCold(benchmark::State &state) {
    setup_lookup_bench();
    adaptive_hot_cache cold;
    cold.init(g_table->count);
    sig_t sg = fn_sig<int(int)>::value;
    for (auto _ : state) {
        const char *nm = hot_name(state.iterations() % 20);
        const entry *e = lookup_adaptive(*g_table, cold, nm, Reflect::Utils::cstr32(nm), sg);
        benchmark::DoNotOptimize(e);
    }
    cold.destroy();
}
BENCHMARK(BM_Resolve_AdaptiveCold);

// --- Random access (no hot entries) ---
static void BM_Resolve_Linear_Random(benchmark::State &state) {
    setup_lookup_bench();
    sig_t sg = fn_sig<int(int)>::value;
    for (auto _ : state) {
        const char *nm = cold_name((int)state.iterations() % 1'000);
        const entry *e = lookup_linear(*g_table, nm, Reflect::Utils::cstr32(nm), sg);
        benchmark::DoNotOptimize(e);
    }
}
BENCHMARK(BM_Resolve_Linear_Random);