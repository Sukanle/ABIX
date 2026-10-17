#include <benchmark/benchmark.h>
#include <new>
#include <stdio.h>
#include <string>
#include <vector>

#include "bench_common.hpp"

#if defined(SKL_ABIX_WINDOWS)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <libloaderapi.h>
#  include <stringapiset.h>
#  include <windows.h>
#endif

// --- BM_Reload_AlternatingPaths_WithPathLookup ---
//
// Same `Object::reload()` loop as BM_Reload_AlternatingPaths below, but the
// target path is resolved with dll_path() *inside* the loop, so the loader-path
// resolution (GetModuleFileName + string building) is included. Kept as a
// separate case because that resolution is not free.
static void BM_Reload_AlternatingPaths_WithPathLookup(benchmark::State &state) {
    const std::string path = dll_path("reload_dll_a");
    skl::abix::dll::Object lib;
    if (!require_plugin(state, lib, "reload_dll_a")) return;

    for (auto _ : state) {
        lib.reload(dll_path("reload_dll_b").c_str());
        lib.reload(path.c_str());
    }
}
BENCHMARK(BM_Reload_AlternatingPaths_WithPathLookup);

// --- BM_Reload_AlternatingPaths ---
//
// `Object::reload()` end to end, alternating between two *different* files. This
// is the case where each swap forces a full map/unmap of a different image, so it
// measures the loader + retire + synchronize path, not a rebuild-in-place
// hot-reload (for that see BM_Reload_SamePath).
static void BM_Reload_AlternatingPaths(benchmark::State &state) {
    const std::string path_a = dll_path("reload_dll_a");
    const std::string path_b = dll_path("reload_dll_b");
    skl::abix::dll::Object lib;
    if (!require_plugin(state, lib, "reload_dll_a")) return;

    for (auto _ : state) {
        lib.reload(path_b.c_str());
        lib.reload(path_a.c_str());
    }
}
BENCHMARK(BM_Reload_AlternatingPaths);

// --- BM_EBR_Synchronize (full epoch advance) ---
static void BM_EBR_Synchronize(benchmark::State &state) {
    auto &dom = skl::abix::rcu::Domain::instance();
    for (auto _ : state) {
        dom.synchronize();
    }
}
BENCHMARK(BM_EBR_Synchronize);

// --- BM_EBR_TryCollect_Empty (try_collect when nothing to reclaim) ---
static void BM_EBR_TryCollect_Empty(benchmark::State &state) {
    auto &dom = skl::abix::rcu::Domain::instance();
    for (auto _ : state) {
        dom.try_collect();
    }
}
BENCHMARK(BM_EBR_TryCollect_Empty);

// ============================================================
// Split: where does a ~22.5 us reload actually spend its time?
//
// `BM_Reload_AlternatingPaths` above measures `Object::reload()` end to end:
// LoadLibrary the new image, swap it in, retire the old image, then
// synchronize(). These three benchmarks split that cost so the optimization
// target is picked from data instead of guessed:
//   BM_Reload_LoadFreeA   — the loader primitive as the code uses it today
//   BM_Reload_LoadFreeExW — the same through LoadLibraryExW + absolute wide path
//   BM_Reload_RetireSync  — RCU retire + grace period + reclaim, no loading
// Windows-only APIs are compiled conditionally; the RCU split is portable.
// ============================================================

static void reclaim_trivial(void *p) noexcept { delete static_cast<skl::abix::dll::Image *>(p); }

static void BM_Reload_LoadFreeA(benchmark::State &state) {
    const std::string path = dll_path("reload_dll_a");
    for (auto _ : state) {
        auto m = skl::abix::dll::detail::load_module(path.c_str());
        if (!m) {
            state.SkipWithError("cannot load reload_dll_a");
            return;
        }
        benchmark::DoNotOptimize(m);
        skl::abix::dll::detail::unload_module(m);
    }
}
BENCHMARK(BM_Reload_LoadFreeA);

// Separate the two halves of the loader pair with PauseTiming: `LoadFreeA` above
// measures the pair, these two attribute it to load and to free.
static void BM_Reload_LoadOnly(benchmark::State &state) {
    const std::string path = dll_path("reload_dll_a");
    for (auto _ : state) {
        auto m = skl::abix::dll::detail::load_module(path.c_str());
        if (!m) {
            state.SkipWithError("cannot load reload_dll_a");
            return;
        }
        benchmark::DoNotOptimize(m);
        state.PauseTiming();
        skl::abix::dll::detail::unload_module(m);
        state.ResumeTiming();
    }
}
BENCHMARK(BM_Reload_LoadOnly);

static void BM_Reload_FreeOnly(benchmark::State &state) {
    const std::string path = dll_path("reload_dll_a");
    for (auto _ : state) {
        state.PauseTiming();
        auto m = skl::abix::dll::detail::load_module(path.c_str());
        state.ResumeTiming();
        if (!m) {
            state.SkipWithError("cannot load reload_dll_a");
            return;
        }
        skl::abix::dll::detail::unload_module(m);
    }
}
BENCHMARK(BM_Reload_FreeOnly);

// `Object::reload` also resolves the table getter after loading; include that.
static void BM_Reload_LoadResolveFree(benchmark::State &state) {
    const std::string path = dll_path("reload_dll_a");
    for (auto _ : state) {
        auto m = skl::abix::dll::detail::load_module(path.c_str());
        if (!m) {
            state.SkipWithError("cannot load reload_dll_a");
            return;
        }
        auto getter = reinterpret_cast<const skl::abix::runtime::Table *(*)()>(
            skl::abix::dll::detail::resolve_symbol(m, SKL_ABIX_TABLE_GETTER));
        const skl::abix::runtime::Table *t = getter ? getter() : nullptr;
        benchmark::DoNotOptimize(t);
        skl::abix::dll::detail::unload_module(m);
    }
}
BENCHMARK(BM_Reload_LoadResolveFree);

// `Object::reload` loads the *other* variant while the current one stays
// resident. This benchmark isolates whether co-residency (and any base-address
// conflict between the two same-source modules) accounts for the gap against
// BM_Reload_LoadFreeA.
static void BM_Reload_LoadSecondWhileFirstResident(benchmark::State &state) {
    const std::string a = dll_path("reload_dll_a");
    const std::string b = dll_path("reload_dll_b");
    auto resident = skl::abix::dll::detail::load_module(a.c_str());
    if (!resident) {
        state.SkipWithError("cannot load reload_dll_a");
        return;
    }
    for (auto _ : state) {
        auto m = skl::abix::dll::detail::load_module(b.c_str());
        if (!m) {
            state.SkipWithError("cannot load reload_dll_b");
            break;
        }
        benchmark::DoNotOptimize(m);
        skl::abix::dll::detail::unload_module(m);
    }
    skl::abix::dll::detail::unload_module(resident);
}
BENCHMARK(BM_Reload_LoadSecondWhileFirstResident);

// The actual hot-reload loop: rebuild in place and reload the *same* path, so
// the module stays mapped (LoadLibrary just bumps the refcount). This is the
// case a developer loop hits, and it is what `BM_Reload_AlternatingPaths`'s a/b
// alternation hides.
static void BM_Reload_SamePath(benchmark::State &state) {
    const std::string path = dll_path("reload_dll_a");
    skl::abix::dll::Object lib;
    if (!require_plugin(state, lib, "reload_dll_a")) return;
    for (auto _ : state) {
        if (!lib.reload(path.c_str())) {
            state.SkipWithError("same-path reload failed");
            return;
        }
    }
}
BENCHMARK(BM_Reload_SamePath);

// --- Bounded reclaim: the same loops with `set_reclaim_timeout_ms` ---------------
//
// Enabling a bound makes every reload ask for a real grace period instead of sharing
// the batched one, which is the documented trade: the retired image (and therefore
// its module) is released on every swap rather than up to SKL_ABIX_RCU_EPOCH_BATCH
// swaps later. The point of these two cases is to measure that trade on the real
// Object path - the unbounded numbers next to them are the default behaviour.
static void BM_Reload_SamePath_BoundedReclaim(benchmark::State &state) {
    const std::string path = dll_path("reload_dll_a");
    skl::abix::dll::Object lib;
    if (!require_plugin(state, lib, "reload_dll_a")) return;
    lib.set_reclaim_timeout_ms(1'000);
    for (auto _ : state) {
        if (!lib.reload(path.c_str())) {
            state.SkipWithError("same-path reload failed");
            return;
        }
    }
    benchmark::DoNotOptimize(lib.reclaim_outcome());
}
BENCHMARK(BM_Reload_SamePath_BoundedReclaim);

static void BM_Reload_AlternatingPaths_BoundedReclaim(benchmark::State &state) {
    const std::string path_a = dll_path("reload_dll_a");
    const std::string path_b = dll_path("reload_dll_b");
    skl::abix::dll::Object lib;
    if (!require_plugin(state, lib, "reload_dll_a")) return;
    lib.set_reclaim_timeout_ms(1'000);
    for (auto _ : state) {
        lib.reload(path_b.c_str());
        lib.reload(path_a.c_str());
    }
    benchmark::DoNotOptimize(lib.reclaim_outcome());
}
BENCHMARK(BM_Reload_AlternatingPaths_BoundedReclaim);

#if defined(SKL_ABIX_WINDOWS)
static void BM_Reload_LoadFreeExW(benchmark::State &state) {
    const std::string path = dll_path("reload_dll_a");
    const int wide_len = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wide_len <= 0) {
        state.SkipWithError("cannot convert the module path to UTF-16");
        return;
    }
    std::vector<wchar_t> wide(static_cast<size_t>(wide_len));
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), wide_len);
    for (auto _ : state) {
        HMODULE m = LoadLibraryExW(wide.data(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!m) {
            state.SkipWithError("LoadLibraryExW failed");
            return;
        }
        benchmark::DoNotOptimize(m);
        FreeLibrary(m);
    }
}
BENCHMARK(BM_Reload_LoadFreeExW);
#endif

static void BM_Reload_RetireSync(benchmark::State &state) {
    auto &dom = skl::abix::rcu::Domain::instance();
    for (auto _ : state) {
        auto *img = new (std::nothrow) skl::abix::dll::Image{};
        if (!img) {
            state.SkipWithError("allocation failed");
            return;
        }
        dom.retire(img, reclaim_trivial);
        dom.synchronize();
    }
}
BENCHMARK(BM_Reload_RetireSync);
