#include "test_common.hpp"

TEST_CASE("8.lookup_policy_benchmark", "[perf][prompt8]") {
    log_info("Test 8: lookup policy benchmark - Linear / StaticHot / AdaptiveHot strategies");
    dll_object lib;
    if (!lib.load(dll_path("hotcache_dll").c_str())) {
        WARN("missing hotcache_dll, skip performance test");
        return;
    }
    const table *t = lib.get_table();
    REQUIRE(t->count == 1'000u + 20u);
    log_info("hotcache_dll table entry count = %u (1000 regular + 20 hot)", t->count);

    constexpr int CALLS = 1'000'000;
    const int HOT_IDS[] = {1'000, 1'001, 1'002, 1'003, 1'004, 1'005, 1'006, 1'007, 1'008, 1'009, 1'010, 1'011, 1'012,
        1'013, 1'014, 1'015, 1'016, 1'017, 1'018, 1'019};

    auto bench = [&](const std::function<const entry *(const char *, name_hash_t, sig_t)> &look,
                     const std::function<const char *(int)> &name_for) {
        double t0 = ns();
        volatile uintptr_t sink = 0;
        sig_t sg = fn_sig<int(int)>::value;
        for (int i = 0; i < CALLS; ++i) {
            const char *nm = name_for(i);
            auto e = look(nm, Reflect::Utils::cstr32(nm), sg);
            if (e) sink ^= reinterpret_cast<uintptr_t>(e->fnptr);
        }
        (void)sink;
        return ns() - t0;
    };

    auto linear_look = [&](const char *nm, name_hash_t hh, sig_t sg2) { return lookup_linear(*t, nm, hh, sg2); };

    static_hot_cache static_cache;
    for (int id : HOT_IDS)
        static_cache.add(id);
    auto static_look = [&](const char *nm, name_hash_t hh, sig_t sg2) {
        return lookup_static_hot(*t, static_cache, nm, hh, sg2);
    };

    adaptive_hot_cache adaptive_cache;
    adaptive_cache.init(t->count);
    auto adaptive_look = [&](const char *nm, name_hash_t hh, sig_t sg2) {
        return lookup_adaptive(*t, adaptive_cache, nm, hh, sg2);
    };

    SECTION("8a.100pct_hot_warm") {
        log_info("8a: 100%% hot hits (after warm-up), verifying the StaticHot speedup");
        for (int i = 0; i < 20'000; ++i) {
            const char *nm = hot_name(i % 20);
            adaptive_look(nm, Reflect::Utils::cstr32(nm), fn_sig<int(int)>::value);
        }
        auto name_20 = [](int i) -> const char * { return hot_name(i % 20); };
        double linear_t = bench(linear_look, name_20);
        double static_t = bench(static_look, name_20);
        double adaptive_t = bench(adaptive_look, name_20);

        log_info("100%% hot warm-up: linear=%.1fms, static=%.1fms, adaptive=%.1fms", linear_t * 1e-6, static_t * 1e-6,
            adaptive_t * 1e-6);
        log_info("speedup: linear/static=%.1fx, linear/adaptive=%.1fx", (static_t > 0 ? linear_t / static_t : 0.0),
            (adaptive_t > 0 ? linear_t / adaptive_t : 0.0));
        if (static_t > 0) REQUIRE(linear_t / static_t > 2.0);
        if (adaptive_t > 0) REQUIRE(adaptive_t < linear_t * 6.0);
    }

    SECTION("8b.adaptive_cold_start") {
        log_info("8b: Adaptive cold start - no warm-up, verifying first-call performance is close to Linear");
        adaptive_hot_cache cold;
        cold.init(t->count);
        auto cold_look = [&](const char *nm, name_hash_t hh, sig_t sg2) {
            return lookup_adaptive(*t, cold, nm, hh, sg2);
        };
        auto name_20 = [](int i) -> const char * { return hot_name(i % 20); };
        double linear_t = bench(linear_look, name_20);
        double cold_t = bench(cold_look, name_20);

        log_info("cold start: linear=%.1fms, adaptive_cold=%.1fms", linear_t * 1e-6, cold_t * 1e-6);
        log_info("cold start overhead ratio: cold/linear=%.2fx", (linear_t > 0 ? cold_t / linear_t : 0.0));
        if (cold_t > 0 && linear_t > 0) REQUIRE(cold_t < linear_t * 8.0);
        cold.destroy();
    }

    SECTION("8c.hot_drift") {
        log_info("8c: hot drift - first 500k hot A (1000-1019), last 500k hot B (500-519)");
        adaptive_hot_cache drift;
        drift.init(t->count);
        auto drift_look = [&](const char *nm, name_hash_t hh, sig_t sg2) {
            return lookup_adaptive(*t, drift, nm, hh, sg2);
        };
        for (int i = 0; i < 20'000; ++i) {
            const char *nm = hot_name(i % 20);
            drift_look(nm, Reflect::Utils::cstr32(nm), fn_sig<int(int)>::value);
        }

        double t0 = ns();
        volatile uintptr_t sink = 0;
        sig_t sg = fn_sig<int(int)>::value;
        for (int i = 0; i < 500'000; ++i) {
            const char *nm = hot_name(i % 20);
            auto e = drift_look(nm, Reflect::Utils::cstr32(nm), sg);
            if (e) sink ^= reinterpret_cast<uintptr_t>(e->fnptr);
        }
        for (int i = 0; i < 500'000; ++i) {
            const char *nm = cold_name(500 + (i % 20));
            auto e = drift_look(nm, Reflect::Utils::cstr32(nm), sg);
            if (e) sink ^= reinterpret_cast<uintptr_t>(e->fnptr);
        }
        double drift_t = ns() - t0;
        (void)sink;

        double static_drift_t = 0;
        {
            double ts = ns();
            volatile uintptr_t sink2 = 0;
            for (int i = 0; i < 500'000; ++i) {
                const char *nm = hot_name(i % 20);
                auto e = static_look(nm, Reflect::Utils::cstr32(nm), sg);
                if (e) sink2 ^= reinterpret_cast<uintptr_t>(e->fnptr);
            }
            for (int i = 0; i < 500'000; ++i) {
                const char *nm = cold_name(500 + (i % 20));
                auto e = static_look(nm, Reflect::Utils::cstr32(nm), sg);
                if (e) sink2 ^= reinterpret_cast<uintptr_t>(e->fnptr);
            }
            static_drift_t = ns() - ts;
            (void)sink2;
        }

        log_info("hot drift: adaptive=%.1fms, static(fixed)=%.1fms", drift_t * 1e-6, static_drift_t * 1e-6);
        if (static_drift_t > 0) log_info("after drift adaptive/static=%.2fx", drift_t / static_drift_t);
        drift.destroy();
    }

    SECTION("8d.real_distribution") {
        log_info("8d: real distribution - 80%% hit 20 hot entries, 20%% random access over the remaining 1000");
        adaptive_hot_cache real_ad;
        real_ad.init(t->count);
        auto real_ad_look = [&](const char *nm, name_hash_t hh, sig_t sg2) {
            return lookup_adaptive(*t, real_ad, nm, hh, sg2);
        };
        for (int i = 0; i < 20'000; ++i) {
            const char *nm = hot_name(i % 20);
            real_ad_look(nm, Reflect::Utils::cstr32(nm), fn_sig<int(int)>::value);
        }

        auto name_80_20 = [](int i) -> const char * {
            if (i % 100 < 80) return hot_name(i % 20);
            return cold_name(i % 1'000);
        };

        double linear_t = bench(linear_look, name_80_20);
        double static_t = bench(static_look, name_80_20);
        double adaptive_t = bench(real_ad_look, name_80_20);

        log_info("80/20 distribution: linear=%.1fms, static=%.1fms, adaptive=%.1fms", linear_t * 1e-6, static_t * 1e-6,
            adaptive_t * 1e-6);
        log_info("speedup: linear/static=%.1fx, linear/adaptive=%.1fx", (static_t > 0 ? linear_t / static_t : 0.0),
            (adaptive_t > 0 ? linear_t / adaptive_t : 0.0));
        real_ad.destroy();
    }
}