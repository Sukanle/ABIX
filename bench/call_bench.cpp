#include <benchmark/benchmark.h>
#include <functional>
#include <stdio.h>

#include "bench_common.hpp"

using namespace skl::abix;

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

static dll_object *g_math_lib = nullptr;
static dll_object *g_hotcache_lib = nullptr;
static dll_func<int(int, int)> g_add;
static dll_func<int(int)> g_e_0000;

static void setup_call_bench() {
    if (!g_math_lib) {
        g_math_lib = new dll_object();
        g_math_lib->load(dll_path("math_dll").c_str());
        g_add = dll_func<int(int, int)>(*g_math_lib, "add");
    }
    if (!g_hotcache_lib) {
        g_hotcache_lib = new dll_object();
        g_hotcache_lib->load(dll_path("hotcache_dll").c_str());
        g_e_0000 = dll_func<int(int)>(*g_hotcache_lib, "e_0000");
    }
}

static void teardown_call_bench() {
    if (g_math_lib) {
        g_add = dll_func<int(int, int)>();
        delete g_math_lib;
        g_math_lib = nullptr;
    }
    if (g_hotcache_lib) {
        g_e_0000 = dll_func<int(int)>();
        delete g_hotcache_lib;
        g_hotcache_lib = nullptr;
    }
}

static void BM_ABIX_Call(benchmark::State &state) {
    setup_call_bench();
    int a = 1, b = 2;
    for (auto _ : state) {
        int r = g_add(a, b);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_ABIX_Call);

static void BM_ABIX_TinyFunction(benchmark::State &state) {
    setup_call_bench();
    int a = 1, b = 2;
    for (auto _ : state) {
        int r = g_add(a, b);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_ABIX_TinyFunction);

static void BM_ABIX_SmallFunction(benchmark::State &state) {
    setup_call_bench();
    int v = 42;
    for (auto _ : state) {
        int r = g_e_0000(v);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_ABIX_SmallFunction);

static void BM_ABIX_Call_Raw(benchmark::State &state) {
    setup_call_bench();
    int a = 1, b = 2;
    auto fn = g_add.raw();
    for (auto _ : state) {
        int r = fn(a, b);
        benchmark::DoNotOptimize(r);
    }
}
BENCHMARK(BM_ABIX_Call_Raw);