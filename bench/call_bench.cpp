#include <benchmark/benchmark.h>
#include <atomic>
#include <cstring>
#include <functional>
#include <stdio.h>

#include "bench_common.hpp"

static int add_direct(int a, int b) { return a + b; }

static int medium_workload(int a, int b) {
    int r = a + b;
    r = (r * 7) ^ (r << 3);
    r = (r * 13) + (r >> 2);
    r = r ^ (r << 5);
    r = (r * 31) - (r >> 1);
    r = r ^ (r << 7);
    r = (r * 17) + (r >> 3);
    r = r ^ (r << 11);
    r = (r * 23) - (r >> 1);
    r = r ^ (r << 13);
    r = (r * 29) + (r >> 5);
    return r;
}

static void BM_DirectCall(benchmark::State &state) {
    int a = 1, b = 2;
    for (auto _ : state) {
        int r = add_direct(a, b);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_DirectCall);

static void BM_FunctionPointerCall(benchmark::State &state) {
    int (*fn)(int, int) = add_direct;
    int a = 1, b = 2;
    for (auto _ : state) {
        int r = fn(a, b);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_FunctionPointerCall);

static void BM_StdFunctionCall(benchmark::State &state) {
    std::function<int(int, int)> fn = add_direct;
    int a = 1, b = 2;
    for (auto _ : state) {
        int r = fn(a, b);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_StdFunctionCall);

static void BM_MediumFunction_Direct(benchmark::State &state) {
    int a = 1, b = 2;
    for (auto _ : state) {
        int r = medium_workload(a, b);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_MediumFunction_Direct);

static void BM_MediumFunction_FnPtr(benchmark::State &state) {
    int (*fn)(int, int) = medium_workload;
    int a = 1, b = 2;
    for (auto _ : state) {
        int r = fn(a, b);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_MediumFunction_FnPtr);

ABIX_NOINLINE static int add_direct_ni(int a, int b) { return a + b; }
ABIX_NOINLINE static int medium_workload_ni(int a, int b) {
    int r = a + b;
    r = (r * 7) ^ (r << 3);
    r = (r * 13) + (r >> 2);
    r = r ^ (r << 5);
    r = (r * 31) - (r >> 1);
    r = r ^ (r << 7);
    r = (r * 17) + (r >> 3);
    r = r ^ (r << 11);
    r = (r * 23) - (r >> 1);
    r = r ^ (r << 13);
    r = (r * 29) + (r >> 5);
    return r;
}

static std::function<int(int, int)> g_std_fn_ni = add_direct_ni;

static void BM_DirectCall_NoInline(benchmark::State &state) {
    int a = 1, b = 2;
    for (auto _ : state) {
        int r = add_direct_ni(a, b);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_DirectCall_NoInline);

static void BM_FunctionPointerCall_NoInline(benchmark::State &state) {
    int (*fn)(int, int) = add_direct_ni;
    int a = 1, b = 2;
    for (auto _ : state) {
        int r = fn(a, b);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_FunctionPointerCall_NoInline);

static void BM_StdFunctionCall_NoInline(benchmark::State &state) {
    int a = 1, b = 2;
    for (auto _ : state) {
        int r = g_std_fn_ni(a, b);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_StdFunctionCall_NoInline);

static void BM_MediumFunction_Direct_NoInline(benchmark::State &state) {
    int a = 1, b = 2;
    for (auto _ : state) {
        int r = medium_workload_ni(a, b);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_MediumFunction_Direct_NoInline);

static void BM_MediumFunction_FnPtr_NoInline(benchmark::State &state) {
    int (*fn)(int, int) = medium_workload_ni;
    int a = 1, b = 2;
    for (auto _ : state) {
        int r = fn(a, b);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_MediumFunction_FnPtr_NoInline);

static skl::abix::dll::Object *g_math_lib = nullptr;
static skl::abix::dll::Object *g_hotcache_lib = nullptr;
static skl::abix::dll::Function<int(int, int)> g_add;
static skl::abix::dll::Function<int(int)> g_e_0000;

// Loads the plugins the call benchmarks need. Returns false (after marking the
// benchmark skipped) when a module is unavailable — the resolved Functions
// would otherwise point into a module that was never mapped.
static bool setup_call_bench(benchmark::State &state) {
    if (!g_math_lib) {
        g_math_lib = new skl::abix::dll::Object();
        if (!require_plugin(state, *g_math_lib, "math_dll")) return false;
        g_add = skl::abix::dll::Function<int(int, int)>(*g_math_lib, "add");
    }
    if (!g_hotcache_lib) {
        g_hotcache_lib = new skl::abix::dll::Object();
        if (!require_plugin(state, *g_hotcache_lib, "hotcache_dll")) return false;
        g_e_0000 = skl::abix::dll::Function<int(int)>(*g_hotcache_lib, "e_0000");
    }
    return true;
}

static void BM_ABIX_Call(benchmark::State &state) {
    if (!setup_call_bench(state)) return;
    int a = 1, b = 2;
    for (auto _ : state) {
        int r = g_add(a, b);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_ABIX_Call);

static void BM_ABIX_TinyFunction(benchmark::State &state) {
    if (!setup_call_bench(state)) return;
    int a = 1, b = 2;
    for (auto _ : state) {
        int r = g_add(a, b);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_ABIX_TinyFunction);

static void BM_ABIX_SmallFunction(benchmark::State &state) {
    if (!setup_call_bench(state)) return;
    int v = 42;
    for (auto _ : state) {
        int r = g_e_0000(v);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_ABIX_SmallFunction);

static void BM_ABIX_Call_Raw(benchmark::State &state) {
    if (!setup_call_bench(state)) return;
    int a = 1, b = 2;
    auto fn = g_add.raw();
    for (auto _ : state) {
        int r = fn(a, b);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_ABIX_Call_Raw);

// --- Decomposition of the per-call cost in Function::operator() -------------
//
// `Function::operator()` (ABIX/DLL/Function.h) does, per call: a state load, an
// RCU enter/exit bracket, a bounds check, a hash+sig verification, a strcmp of
// the entry name, then an indirect call. These two benchmarks isolate the parts
// so the ~6 ns delta against BM_ABIX_Call_Raw can be attributed instead of
// guessed:
//   BM_ABIX_HandleRcuBracket — enter() + indirect call + exit() (no validation)
//   BM_Strcmp_ShortName      — one strcmp of a short symbol name
static void BM_ABIX_HandleRcuBracket(benchmark::State &state) {
    if (!setup_call_bench(state)) return;
    // Resolve the raw pointer once: raw() itself takes a writer lock, which is
    // not part of what operator() does on the fast path.
    auto fn = g_add.raw();
    auto &dom = skl::abix::rcu::Domain::instance();
    int a = 1, b = 2;
    for (auto _ : state) {
        dom.enter();
        int r = fn(a, b);
        dom.exit();
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_ABIX_HandleRcuBracket);

// The name buffer is filled at run time so the comparison cannot be folded.
static char g_strcmp_name[16];

static void BM_Strcmp_ShortName(benchmark::State &state) {
    std::strcpy(g_strcmp_name, "add");
    const char *entry_name = "add";
    for (auto _ : state) {
        int r = std::strcmp(entry_name, g_strcmp_name);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_Strcmp_ShortName);

// --- Prototype: what a "verified snapshot" call fast path can reach ----------
//
// `Function::operator()` re-validates (hash + sig + strcmp) on every call so a
// swapped table is detected immediately. This benchmark models the proposed
// alternative: cache (table, index, raw fn) and decide validity with a single
// acquire load of a generation stamp, falling back to the full check only when
// the stamp moves. It uses public API only, so it measures the achievable floor
// before any production code is touched: the stamp check itself costs about
// 0.1 ns over the bare bracket, so the fast path has real headroom.
//
// The fallback branch is never taken here (the stamp stays 0), which is what a
// steady-state call loop looks like.
static std::atomic<uint32_t> g_table_generation{0};

static void BM_ABIX_Call_Stamped(benchmark::State &state) {
    if (!setup_call_bench(state)) return;
    auto &dom = skl::abix::rcu::Domain::instance();
    const auto *table = g_math_lib->get_table();
    const auto index = g_add.index();
    auto fn = g_add.raw();
    int a = 1, b = 2;
    for (auto _ : state) {
        const uint32_t stamp = g_table_generation.load(std::memory_order_acquire);
        // The branch depends on `stamp`, so the load cannot be elided.
        if (stamp != 0 || table == nullptr || index >= table->count)
            continue;   // cold fallback: the full hash/sig/strcmp re-check runs here
        dom.enter();
        int r = fn(a, b);
        dom.exit();
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_ABIX_Call_Stamped);

// Model: isolate the cost of reaching the Domain through
// `Domain::instance()` on every call (as operator() does) versus holding a
// reference. Identical loop bodies otherwise, so the delta is exactly what
// caching a Domain* in the handle could save.
static void BM_Domain_EnterExit_Hoisted(benchmark::State &state) {
    auto &dom = skl::abix::rcu::Domain::instance();
    for (auto _ : state) {
        dom.enter();
        dom.exit();
    }
}
BENCHMARK(BM_Domain_EnterExit_Hoisted);

static void BM_Domain_EnterExit_Instance(benchmark::State &state) {
    for (auto _ : state) {
        skl::abix::rcu::Domain::instance().enter();
        skl::abix::rcu::Domain::instance().exit();
    }
}
BENCHMARK(BM_Domain_EnterExit_Instance);

// Part of what separates the fast path from the bare bracket: the success path
// still publishes `last_error() = none`. Measure the store alone so the
// remaining gap is attributable.
static void BM_LastError_Store(benchmark::State &state) {
    for (auto _ : state) {
        skl::abix::dll::last_error() = skl::abix::dll::CallError::none;
    }
}
BENCHMARK(BM_LastError_Store);
