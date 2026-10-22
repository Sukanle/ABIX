#include <benchmark/benchmark.h>

#include "bench_common.hpp"
#include "bench_fmt.hpp"

// Shared state: loaded once before benchmarks
static skl::abix::dll::Object *g_lib = nullptr;
static const skl::abix::runtime::Table *g_table = nullptr;
static skl::abix::dll::Image *g_image = nullptr;

// Lazily loads the shared plugin state. Returns true when the benchmarks can
// run; otherwise marks `state` as skipped with the reason instead of letting
// them dereference a null table/image.
static bool ensure_lookup_plugin(benchmark::State &state) {
    if (!g_lib) {
        g_lib = new skl::abix::dll::Object();
        if (!require_plugin(state, *g_lib, "hotcache_dll")) return false;
        g_table = g_lib->get_table();
        g_image = g_lib->image_acquire();
    }
    if (g_table == nullptr || g_image == nullptr) {
        state.SkipWithError("hotcache_dll exposes no name+sig table");
        return false;
    }
    return true;
}

// Pre-formatted name tables. `bench_common.hpp`'s `hot_name()`/`cold_name()` rebuild
// the name on **every** call, which in a ~3 ns lookup is most of the
// measurement: measured that way the read-side bracket (1.64 ns, see
// `BM_ABIX_HandleRcuBracket`) is invisible, which is why `BM_FindIndex_Only` and
// `BM_Resolve_WithEBR` used to report the same value. The name has to be held
// constant across the two so only the bracket differs. (`abix_resolve_bench` and
// this file's own `hash_name_short()` already pre-format for the same reason.)
static const char *hot_name_cached(int i) {
    static char names[20][8] = {};
    if (names[0][0] == '\0')
        for (int k = 0; k < 20; ++k)
            format_into(names[k], "h_{:02d}", k);
    return names[i % 20];
}
static const char *cold_name_cached(int i) {
    static char names[1'000][8] = {};
    if (names[0][0] == '\0')
        for (int k = 0; k < 1'000; ++k)
            format_into(names[k], "e_{:04d}", k);
    return names[i % 1'000];
}

// --- BM_FindIndex_Only (baseline) ---
static void BM_FindIndex_Only(benchmark::State &state) {
    if (!ensure_lookup_plugin(state)) return;
    auto sg = skl::abix::dll::FnSig<int(int)>::value;
    // Local cursor: `state.iterations()` is constant inside the loop body.
    int i = 0;
    for (auto _ : state) {
        const char *nm = hot_name_cached(i);
        skl::abix::runtime::index_t idx;
        find_index(*g_table, g_image->index, nm, sg, 0, idx);
        benchmark::DoNotOptimize(idx);
        ++i;
    }
}
BENCHMARK(BM_FindIndex_Only);

// Per-hash cost on *pre-formatted* names held in a contiguous block: cold_name()
// rebuilds its name on every call, which would otherwise dominate the measurement.
static char g_hash_names[1'000][8];
static char g_hash_names16[1'000][17];

static const char *hash_name_short(int i) {
    if (g_hash_names[0][0] == '\0')
        for (int k = 0; k < 1'000; ++k)
            format_into(g_hash_names[k], "e_{:04d}", k);
    return g_hash_names[i % 1'000];
}
static const char *hash_name_long(int i) {
    if (g_hash_names16[0][0] == '\0')
        for (int k = 0; k < 1'000; ++k)
            format_into(g_hash_names16[k], "symbol_{:08d}_v", k);
    return g_hash_names16[i % 1'000];
}

static void BM_Hash_Name_6ch(benchmark::State &state) {
    int i = 0;
    for (auto _ : state) {
        auto nh = skl::abix::runtime::name_hash(hash_name_short(i));
        benchmark::DoNotOptimize(nh);
        ++i;
    }
}
BENCHMARK(BM_Hash_Name_6ch);

static void BM_Hash_Name_16ch(benchmark::State &state) {
    int i = 0;
    for (auto _ : state) {
        auto nh = skl::abix::runtime::name_hash(hash_name_long(i));
        benchmark::DoNotOptimize(nh);
        ++i;
    }
}
BENCHMARK(BM_Hash_Name_16ch);

// Model: the resolve path needs the name hash twice — once for the table
// lookup and once for the per-call validation — so it used to be computed twice
// per resolve. Both variants hash a *varying* cold name (nothing can be hoisted)
// and differ only in whether find_index() hashes again internally.
static void BM_ResolvePath_TwoHashes(benchmark::State &state) {
    if (!ensure_lookup_plugin(state)) return;
    auto sg = skl::abix::dll::FnSig<int(int)>::value;
    int i = 0;
    for (auto _ : state) {
        const char *nm = cold_name_cached(i);
        auto nh = skl::abix::runtime::name_hash(nm);   // what Function::resolve caches
        benchmark::DoNotOptimize(nh);
        skl::abix::runtime::index_t idx;
        find_index(*g_table, g_image->index, nm, sg, 0, idx);   // hashes again
        benchmark::DoNotOptimize(idx);
        ++i;
    }
}
BENCHMARK(BM_ResolvePath_TwoHashes);

static void BM_ResolvePath_OneHash(benchmark::State &state) {
    if (!ensure_lookup_plugin(state)) return;
    auto sg = skl::abix::dll::FnSig<int(int)>::value;
    int i = 0;
    for (auto _ : state) {
        const char *nm = cold_name_cached(i);
        const auto nh = skl::abix::runtime::name_hash(nm);
        skl::abix::runtime::index_t idx;
        find_index(*g_table, g_image->index, nm, nh, sg, 0, idx);   // reuses it
        benchmark::DoNotOptimize(idx);
        ++i;
    }
}
BENCHMARK(BM_ResolvePath_OneHash);

// --- BM_Resolve_Linear ---
static void BM_Resolve_Linear(benchmark::State &state) {
    if (!ensure_lookup_plugin(state)) return;
    auto sg = skl::abix::dll::FnSig<int(int)>::value;
    int i = 0;
    for (auto _ : state) {
        const char *nm = hot_name_cached(i);
        const auto *e = lookup_linear(*g_table, nm, skl::abix::runtime::name_hash(nm), sg);
        benchmark::DoNotOptimize(e);
        ++i;
    }
}
BENCHMARK(BM_Resolve_Linear);

// --- BM_Resolve_WithEBR ---
static void BM_Resolve_WithEBR(benchmark::State &state) {
    if (!ensure_lookup_plugin(state)) return;
    skl::abix::rcu::Domain &dom = skl::abix::rcu::Domain::instance();
    auto sg = skl::abix::dll::FnSig<int(int)>::value;
    int i = 0;
    for (auto _ : state) {
        dom.enter();
        skl::abix::dll::Image *img = g_lib->image_acquire();
        // Keep the acquire observable: without it the compiler may fold the image
        // lookup away and the benchmark stops measuring the read-side path.
        benchmark::DoNotOptimize(img);
        const char *nm = hot_name_cached(i);
        skl::abix::runtime::index_t idx = 0;
        if (img && img->export_table) {
            find_index(*img->export_table, img->index, nm, sg, 0, idx);
        }
        benchmark::DoNotOptimize(idx);
        dom.exit();
        ++i;
    }
}
BENCHMARK(BM_Resolve_WithEBR);

// --- 80/20 distribution ---
//
// The target name must come from a local cursor: `benchmark::State::iterations()`
// is constant inside the loop body (the harness only updates it between runs), so
// deriving the name from it pins one name for the whole run. abix_lookup_cross_bench
// already uses the local-cursor pattern for the same reason.
static void BM_Resolve_Linear_80_20(benchmark::State &state) {
    if (!ensure_lookup_plugin(state)) return;
    auto sg = skl::abix::dll::FnSig<int(int)>::value;
    int i = 0;
    for (auto _ : state) {
        const char *nm = (i % 100 < 80) ? hot_name(i % 20) : cold_name(i % 1'000);
        const auto *e = lookup_linear(*g_table, nm, skl::abix::runtime::name_hash(nm), sg);
        benchmark::DoNotOptimize(e);
        ++i;
    }
}
BENCHMARK(BM_Resolve_Linear_80_20);

// --- Random access (no hot entries) ---
static void BM_Resolve_Linear_Random(benchmark::State &state) {
    if (!ensure_lookup_plugin(state)) return;
    auto sg = skl::abix::dll::FnSig<int(int)>::value;
    int i = 0;
    for (auto _ : state) {
        const char *nm = cold_name_cached(i);
        const auto *e = lookup_linear(*g_table, nm, skl::abix::runtime::name_hash(nm), sg);
        benchmark::DoNotOptimize(e);
        ++i;
    }
}
BENCHMARK(BM_Resolve_Linear_Random);

#ifndef ALL_BENCHMARKS
int main(int argc, char **argv) {
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
