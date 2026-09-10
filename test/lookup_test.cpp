#include "test_common.hpp"

TEST_CASE("8.lookup_policy_benchmark", "[perf][prompt8]") {
    log_info("Test 8: lookup policy benchmark - Linear strategy");
    skl::abix::dll_object lib;
    if (!lib.load(dll_path("hotcache_dll").c_str())) {
        WARN("missing hotcache_dll, skip performance test");
        return;
    }
    const skl::abix::table *t = lib.get_table();
    REQUIRE(t->count == 1'000u + 20u);
    log_info("hotcache_dll table entry count = %u (1000 regular + 20 hot)", t->count);

    constexpr int CALLS = 1'000'000;

    auto bench = [&](const std::function<const skl::abix::entry *(const char *, skl::abix::name_hash_t, skl::abix::sig_t)> &look,
                     const std::function<const char *(int)> &name_for) {
        double t0 = ns();
        volatile uintptr_t sink = 0;
        skl::abix::sig_t sg = skl::abix::fn_sig<int(int)>::value;
        for (int i = 0; i < CALLS; ++i) {
            const char *nm = name_for(i);
            auto e = look(nm, Reflect::Utils::cstr32(nm), sg);
            if (e) sink ^= reinterpret_cast<uintptr_t>(e->fnptr);
        }
        (void)sink;
        return ns() - t0;
    };

    auto linear_look = [&](const char *nm, skl::abix::name_hash_t hh, skl::abix::sig_t sg2) { return skl::abix::lookup_linear(*t, nm, hh, sg2); };

    SECTION("8a.linear_hot_warm") {
        log_info("8a: linear lookup on hot entries (after warm-up)");
        auto name_20 = [](int i) -> const char * { return hot_name(i % 20); };
        double linear_t = bench(linear_look, name_20);

        log_info("linear hot: %.1fms", linear_t * 1e-6);
        REQUIRE(linear_t > 0);
    }

    SECTION("8b.real_distribution") {
        log_info("8b: real distribution - 80%% hit 20 hot entries, 20%% random access over the remaining 1000");

        auto name_80_20 = [](int i) -> const char * {
            if (i % 100 < 80) return hot_name(i % 20);
            return cold_name(i % 1'000);
        };

        double linear_t = bench(linear_look, name_80_20);

        log_info("80/20 distribution: linear=%.1fms", linear_t * 1e-6);
        REQUIRE(linear_t > 0);
    }
}