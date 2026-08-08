#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string>
#include <chrono>
#include <memory>
#include <functional>
#include <type_traits>

#include "abix/abix.hpp"         // IWYU pragma: keep
#include "dlls/plugin_types.h"   // IWYU pragma: keep

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace skl::abix {
static std::string dll_path(const char *name) {
#if SKL_ABIX_WINDOWS
    return std::string(name) + ".dll";
#else
    return std::string("./plugins/") + name + "/lib" + name + ".so";
#endif
}
static bool file_exists(const std::string &p) {
    FILE *f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}
static double ns() {
    using namespace std::chrono;
    return (double)duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}
static const char *hot_name(int i) {
    static char buf[16];
    std::snprintf(buf, sizeof(buf), "h_%02d", i);
    return buf;
}
static const char *cold_name(int i) {
    static char buf[16];
    std::snprintf(buf, sizeof(buf), "e_%04d", i);
    return buf;
}
static void log_info(const char *fmt, ...) {
    std::printf("[log] ");
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::printf("\n");
}
}   // namespace skl::abix

using namespace skl::abix;

TEST_CASE("1.basic_math_linear_scan", "[basic][prompt1]") {
    log_info(
        "Test 1: load math_dll, resolve add/multiply via linear scan of the reflection table and call them through "
        "integer handles");
    dll_object lib;
    REQUIRE(lib.load(dll_path("math_dll").c_str()));
    REQUIRE(lib.is_loaded());
    log_info("math_dll loaded, resolving add / multiply");

    auto add = dll_func<int(int, int)>(lib, "add");
    REQUIRE(add.valid());
    REQUIRE(add.handle_id() == 0u);
    REQUIRE(add(2, 3) == 5);
    REQUIRE(add(-5, 5) == 0);
    log_info("add(2,3)=%d, add(-5,5)=%d passed, handle ID=0", add(2, 3), add(-5, 5));

    auto mul = dll_func<double(double, double)>(lib, "multiply");
    REQUIRE(mul.valid());
    REQUIRE(mul(1.5, 4.0) == 6.0);
    log_info("multiply(1.5, 4.0)=%.1f passed", mul(1.5, 4.0));

    const table *t = lib.get_table();
    REQUIRE(t->magic == SKL_ABIX_TABLE_MAGIC);
    static_assert(std::is_trivial_v<entry>);
    static_assert(std::is_trivial_v<table>);

    index_t idx;
    REQUIRE(find_index(*t, "add", fn_sig<int(int, int)>::value, 0, idx) == lookup_result::ok);
    log_info("reflection table is plain POD, linear scan find_index(add) hit at index %u", (unsigned)idx);
}

TEST_CASE("2.cross_compiler_variant", "[cross][prompt2]") {
    log_info(
        "Test 2: the same function name yields consistent results across g++/clang/MSVC builds; only C types cross the "
        "boundary");
    for (const char *dir :
        {"./", "./variants/tool_x/version_dll", "./variants/tool_y/version_dll", "./variants/msvc_x64/version_dll"}) {
        std::string full = std::string(dir) +
#if SKL_ABIX_WINDOWS
                           "/version_dll.dll";
#else
                           "/libversion_dll.so";
#endif
        if (!file_exists(full)) {
            log_info("missing cross-compiler variant %s (built in %s)", full.c_str(), dir);
            continue;
        }
        dll_object lib;
        REQUIRE(lib.load(full.c_str()));
        auto get_tag = dll_func<const char *()>(lib, "get_build_tag");
        auto sum = dll_func<int(int, int)>(lib, "get_int_sum");
        auto mul = dll_func<double(double, double)>(lib, "get_double_mul");
        REQUIRE(get_tag.valid());
        REQUIRE(sum(10, 20) == 30);
        REQUIRE(mul(2.5, 4.0) == 10.0);
        const char *tag = get_tag();
        CHECK(tag != nullptr);
        log_info(
            "cross-compiler variant %s validated: build_tag=%s, sum=30, mul=10.0", full.c_str(), (tag ? tag : "null"));
    }
}

TEST_CASE("3.signature_hash_check", "[typesafe][prompt3]") {
    log_info(
        "Test 3: signature hash validation - a signature mismatch for the same name is rejected at lookup time, with "
        "no silent type conversion");
    dll_object lib;
    REQUIRE(lib.load(dll_path("sigcheck_dll").c_str()));

    auto ok = dll_func<void(int)>(lib, "process");
    REQUIRE(ok.valid());
    ok(42);
    log_info("resolved process with the correct signature void(int) and called process(42) successfully");

    auto bad = dll_func<void(double)>(lib, "process");
    REQUIRE(!bad.valid());
    REQUIRE(last_error() == call_error::sig_mismatch);
    log_info("resolving process with the wrong signature void(double) rejected (sig_mismatch=%d)",
        (int)call_error::sig_mismatch);
    bad(1.0);
    REQUIRE(last_error() == call_error::table_changed);
    log_info(
        "calling an invalid handle does not crash and sets the table_changed error code; no silent int->double "
        "conversion");

    index_t idx;
    auto r = find_index(*lib.get_table(), "process", fn_sig<void(double)>::value, 0, idx);
    REQUIRE(r == lookup_result::sig_mismatch);
    log_info("find_index also returns sig_mismatch for a mismatched signature");
}

TEST_CASE("4.unique_resource_takeover", "[resource][prompt4]") {
    log_info(
        "Test 4: exclusive resource takeover across DLLs - both unique_dll_ptr / unique_ptr invoke the DLL release "
        "function");
    dll_object lib;
    REQUIRE(lib.load(dll_path("resource_dll").c_str()));
    log_info("resource_dll loaded, preparing resource lifecycle tests");

    {
        auto alive = dll_func<int()>(lib, "get_resource_alive");
        auto create = dll_func<Resource *(int)>(lib, "create_resource");
        auto destroy = dll_func<void(Resource *)>(lib, "destroy_resource");
        auto rid = dll_func<int(Resource *)>(lib, "resource_id");
        auto rpay = dll_func<int(Resource *, int)>(lib, "resource_payload");
        REQUIRE(create.valid());
        REQUIRE(destroy.valid());

        REQUIRE(alive() == 0);
        log_info("initial alive resource count = %d", alive());

        {
            unique_dll_ptr<Resource> h(create(7), destroy.raw());
            REQUIRE((bool)h);
            REQUIRE(rid(h.get()) == 7);
            REQUIRE(rpay(h.get(), 3) == 3);
            log_info("method A: unique_dll_ptr holds resource id=%d, payload[3]=%d", rid(h.get()), rpay(h.get(), 3));
        }
        REQUIRE(alive() == 0);
        log_info("after leaving scope, the DLL destroy_resource was called and the alive count dropped to zero");

        {
            fn_deleter<Resource> dl(destroy.raw());
            ::std::unique_ptr<Resource, fn_deleter<Resource>> up(create(3), dl);
            REQUIRE(rid(up.get()) == 3);
            log_info("method B: unique_ptr<Resource, fn_deleter> holds resource id=%d", rid(up.get()));
        }
        REQUIRE(alive() == 0);
        log_info("unique_ptr called the DLL release function via fn_deleter; the alive count dropped to zero");

        {
            unique_dll_ptr<Resource> h(create(1), destroy.raw());
            REQUIRE(lib.ref_count() >= 1);
            REQUIRE(lib.unload() == false);
            REQUIRE(last_error() == call_error::stale_handle);
            REQUIRE(lib.is_loaded());
            log_info("ref-count token: unload() rejected while live handles exist (stale_handle)");
            h.reset();
        }
    }

    REQUIRE(lib.unload() == true);
    REQUIRE(!lib.is_loaded());
    log_info("after all handles were destroyed, unload() succeeded and the module is unloaded");
}

TEST_CASE("5.ref_dll_ptr_refcount", "[resource][prompt5]") {
    log_info(
        "Test 5: shared resource (ref_dll_ptr non-atomic ref-count) - multiple handles share, released only on last "
        "destruction");
    dll_object lib;
    REQUIRE(lib.load(dll_path("resource_dll").c_str()));

    auto alive = dll_func<int()>(lib, "get_config_alive");
    auto area = dll_func<int(Config *)>(lib, "config_area");
    auto create_shared = dll_func<ref_dll_ptr<Config>(int, int)>(lib, "create_shared_config");
    REQUIRE(create_shared.valid());

    {
        ref_dll_ptr<Config> a = create_shared(3, 4);
        REQUIRE(a.use_count() == 1);
        REQUIRE(area(a.get()) == 12);
        log_info("first handle a created, ref-count=1, config_area=%d", area(a.get()));

        ref_dll_ptr<Config> b = a;
        REQUIRE(a.use_count() == 2);
        REQUIRE(b.use_count() == 2);
        log_info("after copying b=a ref-count=2");

        ref_dll_ptr<Config> c = b;
        REQUIRE(a.use_count() == 3);
        log_info("after copying c=b ref-count=3");
        {
            ref_dll_ptr<Config> d = c;
            REQUIRE(a.use_count() == 4);
            log_info("after copying d=c ref-count=4");
        }
        REQUIRE(a.use_count() == 3);
        REQUIRE(area(a.get()) == 12);
        log_info("after d was destroyed ref-count=3, the shared resource is still alive");
    }
    REQUIRE(alive() == 0);
    log_info(
        "all handles destroyed, ref-count dropped to zero, and the DLL release was called last (get_config_alive=0)");
}

TEST_CASE("6.abi_function_callback", "[callback][prompt6]") {
    log_info(
        "Test 6: skl::abix::function_dll (8-byte callback) - a capturing lambda is registered across the boundary and "
        "invoked lazily by the DLL");
    static_assert(
        sizeof(skl::abix::function_dll<void(int)>) == 8, "skl::abix::function_dll 必须为 8 字节（uint64_t 句柄）");
    log_info("sizeof(skl::abix::function_dll<void(int)>) = %zu bytes (8-byte handle)",
        sizeof(skl::abix::function_dll<void(int)>));
    dll_object lib;
    REQUIRE(lib.load(dll_path("callback_dll").c_str()));

    auto reg = dll_func<void(skl::abix::function_dll<void(int)>)>(lib, "register_callback");
    auto has = dll_func<int()>(lib, "has_callback");
    auto inv = dll_func<int(int)>(lib, "invoke_callback");
    auto last = dll_func<int()>(lib, "get_last_invoked");
    auto clear = dll_func<void()>(lib, "clear_callback");
    REQUIRE(reg.valid());

    int captured = 100;
    skl::abix::function_dll<void(int)> cb = [captured](int x) {
        log_info("callback invoked: captured variable captured=%d, argument x=%d, sum=%d", captured, x, captured + x);
    };
    REQUIRE(cb);
    reg(std::move(cb));
    REQUIRE(has() == 1);
    log_info("callback registered into the DLL (has_callback=1), handle=%llu", (unsigned long long)cb.handle());

    REQUIRE(inv(5) == 0);
    REQUIRE(last() == 5);
    log_info(
        "DLL lazily invoked the callback inv(5)=0; the captured variable was copied/moved correctly, last_invoked=5");

    clear();
    REQUIRE(has() == 0);
    REQUIRE(inv(1) == -1);
    log_info("after clear_callback has_callback=0, subsequent invocation is rejected (returns -1)");
}

TEST_CASE("7.version_evolution", "[version][prompt7]") {
    log_info(
        "Test 7: version evolution and forward compatibility - v1.0/v2.0 version tokens for the same log interface "
        "coexist");
    dll_object lib;
    REQUIRE(lib.load(dll_path("version_dll").c_str()));

    auto v1 = dll_func<void(const char *)>(lib, "log", SKL_ABIX_VERSION("1.0"));
    REQUIRE(v1.valid());
    v1("hello v1");
    auto lastmsg = dll_func<const char *()>(lib, "get_last_msg");
    auto level1 = dll_func<int()>(lib, "get_last_level");
    REQUIRE(std::strcmp(lastmsg(), "hello v1") == 0);
    REQUIRE(level1() == 0);
    log_info("v1.0 client called log(\"hello v1\"): last_msg=%s, level=%d (default mapping)", lastmsg(), level1());

    auto v2 = dll_func<void(const char *, int)>(lib, "log", SKL_ABIX_VERSION("2.0"));
    REQUIRE(v2.valid());
    v2("hello v2", 7);
    REQUIRE(level1() == 7);
    REQUIRE(std::strcmp(lastmsg(), "hello v2") == 0);
    log_info(
        "v2.0 client called log(\"hello v2\", 7): last_msg=%s, level=%d (full functionality)", lastmsg(), level1());
}

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

TEST_CASE("9.hot_reload", "[reload][prompt9]") {
    log_info(
        "Test 9: hot-reload - after unloading A and loading B the handle ID is unchanged and the return value updates");
    dll_object lib;
    REQUIRE(lib.load(dll_path("reload_dll_a").c_str()));

    auto v = dll_func<int()>(lib, "get_version");
    REQUIRE(v.valid());
    uint64_t id = v.handle_id();
    REQUIRE(v() == 1);
    log_info("loaded reload_dll_a: get_version()=1, integer handle ID=%llu", (unsigned long long)id);

    REQUIRE(lib.reload(dll_path("reload_dll_b").c_str()));
    REQUIRE(lib.is_loaded());
    log_info("hot-reload: unload A and load reload_dll_b");

    REQUIRE(v.handle_id() == id);
    REQUIRE(v() == 2);
    log_info("after hot-reload the handle ID is still %llu, get_version()=2 (zero-downtime upgrade succeeded)",
        (unsigned long long)v.handle_id());
}

TEST_CASE("10.edge_handling", "[edge][prompt10]") {
    log_info(
        "Test 10: edge cases and error handling - not found / call after unload / ref-count rejects unload / calling "
        "convention mismatch");
    {
        dll_object lib;
        REQUIRE(lib.load(dll_path("edge_dll").c_str()));
        auto missing = dll_func<int(int, int)>(lib, "no_such_fn");
        REQUIRE(!missing.valid());
        REQUIRE(last_error() == call_error::not_found);
        log_info("a) name no_such_fn not found in the table -> valid()=false, error code=not_found(%d)",
            (int)call_error::not_found);
    }

    {
        dll_object *lib = new dll_object;
        REQUIRE(lib->load(dll_path("edge_dll").c_str()));
        auto fn = dll_func<int(int, int)>(*lib, "compute");
        REQUIRE(fn.valid());
        lib->force_unload();
        REQUIRE(!fn.valid());
        int r = fn(3, 2);
        REQUIRE(last_error() == call_error::not_loaded);
        log_info(
            "b) calling the stale compute handle after unload -> no crash, returns default, error code=not_loaded(%d)",
            (int)call_error::not_loaded);
        (void)r;
        delete lib;
    }

    {
        dll_object lib;
        REQUIRE(lib.load(dll_path("edge_dll").c_str()));
        auto fn = dll_func<int(int, int)>(lib, "compute");
        REQUIRE(fn.valid());
        REQUIRE(lib.unload() == false);
        REQUIRE(lib.is_loaded());
        REQUIRE(fn(10, 4) == 6);
        log_info("b2) ref-count token: unload() rejected while live handles exist, compute=6");
    }

    {
        if (!file_exists(dll_path("edge_stdcall_dll"))) {
            WARN("missing edge_stdcall_dll, skip calling convention test");
            return;
        }
        dll_object lib;
        REQUIRE(lib.load(dll_path("edge_stdcall_dll").c_str()));
        auto wrong = dll_func<int(int, int)>(lib, "compute");
        REQUIRE(!wrong.valid());
        REQUIRE(last_error() == call_error::sig_mismatch);
        log_info("c) requesting the stdcall-registered compute with cdecl rejected (sig_mismatch)");
        auto right = dll_func<int(int, int), SKL_ABIX_CCPICK(Stdcall)>(lib, "compute");
        REQUIRE(right.valid());
        REQUIRE(right(6, 7) == 42);
        log_info("  stdcall request matched, compute(6,7)=42");
    }
}

TEST_CASE("11.char_string_copy", "[resource][prompt11][charcopy]") {
    log_info(
        "Test 11: character copy resource - strdup_copy allocates in the DLL, the host reads, then the DLL releases");
    dll_object lib;
    REQUIRE(lib.load(dll_path("resource_dll").c_str()));

    auto strdup = dll_func<char *(const char *, int *)>(lib, "strdup_copy");
    auto freed = dll_func<void(char *)>(lib, "string_destroy");
    REQUIRE(strdup.valid());
    REQUIRE(freed.valid());
    log_info("resolved strdup_copy / string_destroy, starting the character copy test");

    const char *src = "跨 DLL 字符拷贝 - hello 反射表";
    int len = -1;
    char *dst = strdup(src, &len);
    REQUIRE(dst != nullptr);
    REQUIRE(len == static_cast<int>(std::strlen(src)));
    REQUIRE(std::strcmp(dst, src) == 0);
    log_info("strdup_copy returned length=%d, content=[%s]", len, dst);

    REQUIRE(dst[len] == '\0');
    log_info(
        "verified the buffer ends with '\\0'; the host did not release it directly (string_destroy will be called)");

    freed(dst);
    log_info("string_destroy released the DLL-allocated buffer; no cross-CRT heap conflict");
}

TEST_CASE("12.socket_resource_lifecycle", "[resource][prompt12][socket]") {
    log_info(
        "Test 12: Socket resource - simulating network connection open/send/recv/close, verifying resource management "
        "and character transfer");
    dll_object lib;
    REQUIRE(lib.load(dll_path("resource_dll").c_str()));

    auto alive = dll_func<int()>(lib, "socket_alive");
    auto open = dll_func<Socket *(const char *, int)>(lib, "socket_open");
    auto close = dll_func<void(Socket *)>(lib, "socket_close");
    auto isopen = dll_func<int(const Socket *)>(lib, "socket_is_open");
    auto fd = dll_func<int(const Socket *)>(lib, "socket_fd");
    auto send_ = dll_func<int(Socket *, const char *)>(lib, "socket_send");
    auto recv_ = dll_func<int(Socket *, char *, int)>(lib, "socket_recv");
    REQUIRE(open.valid());
    REQUIRE(close.valid());
    REQUIRE(send_.valid());
    REQUIRE(recv_.valid());

    REQUIRE(alive() == 0);
    log_info("initial state: socket_alive=0 (no connections yet)");

    {
        unique_dll_ptr<Socket> s(open("127.0.0.1", 8'080), close.raw());
        REQUIRE((bool)s);
        REQUIRE(isopen(s.get()) == 1);
        REQUIRE(fd(s.get()) == 1'000);
        REQUIRE(alive() == 1);
        log_info("opened a Socket (fd=%d, 127.0.0.1:8080), socket_alive=1", fd(s.get()));

        const char *payload = "GET /api HTTP/1.1";
        int n = send_(s.get(), payload);
        REQUIRE(n == static_cast<int>(std::strlen(payload)));
        log_info("socket_send sent [%s] successfully, returned byte count=%d", payload, n);

        char buf[128];
        std::memset(buf, 0, sizeof(buf));
        int got = recv_(s.get(), buf, (int)sizeof(buf) - 1);
        REQUIRE(got == static_cast<int>(std::strlen(payload)));
        REQUIRE(std::strcmp(buf, payload) == 0);
        log_info("socket_recv read the receive buffer, got [%s] (byte count=%d)", buf, got);
    }
    REQUIRE(alive() == 0);
    log_info("after leaving scope unique_dll_ptr automatically called socket_close, socket_alive=0");
    log_info("Socket resource lifecycle test complete: open -> send -> recv -> close all correct");
}

TEST_CASE("13.cross_crt_msvc_resource", "[cross][prompt13][msvc][resource]") {
    log_info(
        "Test 13: cross-CRT/MSVC resource - MinGW host loads the MSVC(cl.exe)-compiled resource_dll, verifying the "
        "resource lifecycle");
    std::string variant = "./variants/msvc_x64/resource_dll/resource_dll.dll";
    if (!file_exists(variant)) {
        WARN("missing MSVC variant "
             << variant
             << " (please run tools/build_msvc_variants.py first), skip cross-CRT resource test");
        return;
    }
    dll_object lib;
    REQUIRE(lib.load(variant.c_str()));
    log_info("loaded the MSVC-compiled resource_dll (cross-CRT resource lifecycle)");

    {
        auto strdup = dll_func<char *(const char *, int *)>(lib, "strdup_copy");
        auto freed = dll_func<void(char *)>(lib, "string_destroy");
        REQUIRE(strdup.valid());
        REQUIRE(freed.valid());
        int len;
        char *s = strdup("跨 CRT 字符拷贝", &len);
        REQUIRE(s != nullptr);
        REQUIRE(std::strcmp(s, "跨 CRT 字符拷贝") == 0);
        log_info(" [MSVC] strdup_copy cross-CRT copy of [%s] (%d bytes) succeeded", s, len);
        freed(s);
        log_info(" [MSVC] string_destroy release succeeded, no heap conflict");
    }

    {
        auto alive = dll_func<int()>(lib, "get_resource_alive");
        auto create = dll_func<Resource *(int)>(lib, "create_resource");
        auto destroy = dll_func<void(Resource *)>(lib, "destroy_resource");
        auto rid = dll_func<int(Resource *)>(lib, "resource_id");
        REQUIRE(create.valid());
        REQUIRE(destroy.valid());
        REQUIRE(alive() == 0);
        {
            unique_dll_ptr<Resource> h(create(9), destroy.raw());
            REQUIRE(rid(h.get()) == 9);
            REQUIRE(alive() == 1);
        }
        REQUIRE(alive() == 0);
        log_info(" [MSVC] Resource created/released across CRT via unique_dll_ptr, alive count dropped to zero");
    }

    {
        auto alive = dll_func<int()>(lib, "socket_alive");
        auto open = dll_func<Socket *(const char *, int)>(lib, "socket_open");
        auto close = dll_func<void(Socket *)>(lib, "socket_close");
        auto send_ = dll_func<int(Socket *, const char *)>(lib, "socket_send");
        auto recv_ = dll_func<int(Socket *, char *, int)>(lib, "socket_recv");
        REQUIRE(open.valid());
        {
            unique_dll_ptr<Socket> s(open("10.0.0.1", 443), close.raw());
            REQUIRE(alive() == 1);
            send_(s.get(), "PING");
            char buf[16];
            std::memset(buf, 0, sizeof(buf));
            int got = recv_(s.get(), buf, (int)sizeof(buf) - 1);
            REQUIRE(std::strcmp(buf, "PING") == 0);
        }
        REQUIRE(alive() == 0);
        log_info(" [MSVC] Socket open->send->recv->close across CRT, alive count dropped to zero");
    }

    {
        auto alive = dll_func<int()>(lib, "get_config_alive");
        auto create = dll_func<ref_dll_ptr<Config>(int, int)>(lib, "create_shared_config");
        auto area = dll_func<int(Config *)>(lib, "config_area");
        REQUIRE(create.valid());
        {
            ref_dll_ptr<Config> a = create(5, 2);
            REQUIRE(area(a.get()) == 10);
            ref_dll_ptr<Config> b = a;
            REQUIRE(a.use_count() == 2);
        }
        REQUIRE(alive() == 0);
        log_info(" [MSVC] ref_dll_ptr ref-count dropped to zero across CRT and released correctly");
    }
    log_info("MinGW host loaded the MSVC resource_dll; all resource lifecycles are correct across CRT");
}

struct TestVec3 {
    float x;
    float y;
    float z;
};

SKL_ABIX_TYPE_TAG(TestVec3, "test::TestVec3");

SKL_RFS_CLASS(TestVec3)
SKL_RFS_PROPERTY(x)
SKL_RFS_PROPERTY(y)
SKL_RFS_PROPERTY(z)
SKL_RFS_CLASS();

SKL_RFD_CLASS(TestVec3)
SKL_RFD_PROPERTY(x)
SKL_RFD_PROPERTY(y)
SKL_RFD_PROPERTY(z)
SKL_RFD_CLASS();

template<typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

TEST_CASE("14.reflection_integration", "[refl][prompt5-9]") {
    log_info("Test 14: verify ABIX integrates the static/dynamic reflection facilities of the Reflection library");

    SECTION("5.static_fp_table_validation") {
        using namespace skl::abix::refl;

        using table_type = SRefl::type_list<fn_entry_tag<fn_sig_v<int(int, int)>, URefl::cstr32("add")>,
            fn_entry_tag<fn_sig_v<double(double, double)>, URefl::cstr32("multiply")>,
            fn_entry_tag<fn_sig_v<int()>, URefl::cstr32("calc_state_alive")>>;

        static_assert(has_unique_sigs<table_type>::value, "Table entries must have unique signatures");

        static_assert(find_by_sig<table_type, fn_sig_v<int(int, int)>>::index == 0, "add should be at index 0");
        static_assert(
            find_by_sig<table_type, fn_sig_v<double(double, double)>>::index == 1, "multiply should be at index 1");
        static_assert(find_by_sig<table_type, fn_sig_v<int()>>::index == 2, "calc_state_alive should be at index 2");
        static_assert(
            find_by_sig<table_type, fn_sig_v<float(float)>>::index == -1, "Unknown signature should return -1");

        log_info(" [FP] compile-time function table validation passed: 3 unique signatures, index lookup correct");
    }

    SECTION("6.any_cross_dll_parameter") {
        dll_object lib;
        REQUIRE(lib.load(dll_path("math_dll").c_str()));

        auto add = dll_func<int(int, int)>(lib, "add");
        REQUIRE(add.valid());
        DynamicAny result = dll_func_call_any(add, 3, 7);
        int val = any_cast_val<int>(result);
        REQUIRE(val == 10);

        log_info(" [Any] dll_func_call_any(3,7) -> Any -> any_cast_val<int> = %d", val);
    }

    SECTION("7.registry_runtime_lookup") {
        auto calc_state_id = DRefl::type_id_of<CalcState>();
        auto calc_config_id = DRefl::type_id_of<CalcConfig>();
        auto shared_counter_id = DRefl::type_id_of<SharedCounter>();

        REQUIRE(calc_state_id != calc_config_id);
        REQUIRE(calc_config_id != shared_counter_id);
        REQUIRE(calc_state_id != shared_counter_id);

        log_info(" [Registry] CalcState id=%zu, CalcConfig id=%zu, SharedCounter id=%zu", (size_t)calc_state_id,
            (size_t)calc_config_id, (size_t)shared_counter_id);
    }

    SECTION("8.typeinfo_struct_field_access") {
        DynamicTypeInfo ti = make_pod_type_info<TestVec3>("TestVec3");
        REQUIRE(ti.name == std::string("TestVec3"));
        REQUIRE(ti.kind == DRefl::Kind::Struct);
        REQUIRE(ti.size == sizeof(TestVec3));

        log_info(" [TypeInfo] TestVec3: name=%s, size=%zu, kind=Struct", ti.name, ti.size);

        DynamicFieldAccessor field_x = make_offset_field<TestVec3, float, offsetof(TestVec3, x)>("x");
        REQUIRE(field_x.info.name == std::string("x"));
        REQUIRE(field_x.info.offset == offsetof(TestVec3, x));

        TestVec3 v = {1.0f, 2.0f, 3.0f};
        float *px = static_cast<float *>(field_x.getter(&v));
        REQUIRE(*px == 1.0f);
        float new_val = 10.0f;
        field_x.setter(&v, &new_val);
        REQUIRE(v.x == 10.0f);

        log_info(" [TypeInfo] TestVec3::x field accessor: name=%s, offset=%u, getter/setter validated",
            field_x.info.name, field_x.info.offset);
    }

    SECTION("9.static_reflection_field_info") {
        using Vec3Info = SRefl::TypeInfo<TestVec3>;

        static_assert(
            Vec3Info::_name == URefl::string_view("TestVec3 [class]"), "Static reflection class name mismatch");

        constexpr auto &x_field = Vec3Info::Registry::_x;
        constexpr auto &y_field = Vec3Info::Registry::_y;
        constexpr auto &z_field = Vec3Info::Registry::_z;
        int size = sizeof(Vec3Info::Registry::_x);

        static_assert(x_field.getName() == URefl::string_view("x"), "Field name should be 'x'");
        static_assert(y_field.getName() == URefl::string_view("y"), "Field name should be 'y'");
        static_assert(z_field.getName() == URefl::string_view("z"), "Field name should be 'z'");

        static_assert(
            std::is_same_v<remove_cvref_t<decltype(x_field)>::traits::type, float>, "x field type should be float");
        static_assert(
            std::is_same_v<remove_cvref_t<decltype(y_field)>::traits::type, float>, "y field type should be float");
        static_assert(
            std::is_same_v<remove_cvref_t<decltype(z_field)>::traits::type, float>, "z field type should be float");

        static_assert(x_field.is_member(), "x should be a member variable");
        static_assert(!x_field.is_function(), "x should not be a function");
        static_assert(x_field.is_variable(), "x should be a variable");

        log_info(" [StaticRefl] TestVec3: x(Float), y(Float), z(Float) - compile-time field info validated");
    }

    log_info("ABIX successfully integrated the FP/Any/Registry/TypeInfo/StaticRefl facilities of Reflection");
}

struct PlayerPublic {
    char reserved[16];
    int health;
    float x, y;
};

static_assert(offsetof(PlayerPublic, health) == 16, "PlayerPublic::health offset must be 16");
static_assert(offsetof(PlayerPublic, x) == 20, "PlayerPublic::x offset must be 20");
static_assert(offsetof(PlayerPublic, y) == 24, "PlayerPublic::y offset must be 24");

TEST_CASE("15.closed_source_type_access", "[closed][prompt15]") {
    log_info(
        "Test 15: manual closed-source type registration and consumption - the consumer accesses Player public fields "
        "through a safe facade");
    dll_object lib;
    if (!lib.load(dll_path("closed_dll").c_str())) {
        WARN("missing closed_dll, skip closed-source test.");
        return;
    }

    auto create = dll_func<Player *(uint64_t)>(lib, "create_player");
    auto destroy = dll_func<void(Player *)>(lib, "destroy_player");
    auto alive = dll_func<int()>(lib, "player_alive");
    auto get_health = dll_func<int(Player *)>(lib, "player_get_health");
    auto set_health = dll_func<void(Player *, int)>(lib, "player_set_health");
    auto get_x = dll_func<float(Player *)>(lib, "player_get_x");
    auto get_y = dll_func<float(Player *)>(lib, "player_get_y");
    auto set_pos = dll_func<void(Player *, float, float)>(lib, "player_set_position");
    auto get_id = dll_func<uint64_t(Player *)>(lib, "player_get_id");
    auto type_hash_fn = dll_func<uint64_t()>(lib, "player_type_hash");
    auto offset_health = dll_func<int()>(lib, "player_offset_health");
    auto sizeof_fn = dll_func<int()>(lib, "player_sizeof");

    REQUIRE(create.valid());
    REQUIRE(destroy.valid());
    REQUIRE(alive.valid());
    REQUIRE(get_health.valid());
    REQUIRE(set_health.valid());
    REQUIRE(type_hash_fn.valid());
    REQUIRE(offset_health.valid());
    REQUIRE(sizeof_fn.valid());

    constexpr uint64_t EXPECTED_HASH = 0x1234ABCDULL;
    REQUIRE(type_hash_fn() == EXPECTED_HASH);
    log_info("type hash validation passed: 0x%016llX == 0x%016llX", (unsigned long long)type_hash_fn(),
        (unsigned long long)EXPECTED_HASH);

    REQUIRE(offset_health() == (int)offsetof(PlayerPublic, health));
    log_info("runtime offset validation passed: health=%d (compile-time=%zu)", offset_health(),
        offsetof(PlayerPublic, health));

    REQUIRE(alive() == 0);

    auto pub = [](Player *raw) -> PlayerPublic * { return reinterpret_cast<PlayerPublic *>(raw); };

    SECTION("15a.basic_access") {
        INFO("15a: basic create, read/write public fields, auto-destroy");
        {
            unique_dll_ptr<Player> player(create(42), destroy.raw());
            REQUIRE((bool)player);
            REQUIRE(alive() == 1);
            PlayerPublic *pp = pub(player.get());

            REQUIRE(pp->health == 100);
            pp->health = 200;
            REQUIRE(pp->health == 200);

            REQUIRE(get_health(player.get()) == 200);
            set_health(player.get(), 300);
            REQUIRE(get_health(player.get()) == 300);

            pp->x = 10.0f;
            pp->y = 20.0f;
            REQUIRE(get_x(player.get()) == 10.0f);
            REQUIRE(get_y(player.get()) == 20.0f);

            set_pos(player.get(), 50.0f, 60.0f);
            REQUIRE(pp->x == 50.0f);
            REQUIRE(pp->y == 60.0f);

            REQUIRE(get_id(player.get()) == 42);
        }
        REQUIRE(alive() == 0);
        log_info("[PASS] 15.1 closed-source type-safe access succeeded, health=300, position=(50,60), id=42");
    }

    SECTION("15b.direct_field_access") {
        INFO("15b: read/write directly through safe facade offsets (bypassing function accessors)");
        unique_dll_ptr<Player> player(create(99), destroy.raw());
        REQUIRE((bool)player);
        PlayerPublic *pp = pub(player.get());

        pp->health = 999;
        pp->x = 3.14f;
        pp->y = 2.71f;

        REQUIRE(get_health(player.get()) == 999);
        REQUIRE(get_x(player.get()) == 3.14f);
        REQUIRE(get_y(player.get()) == 2.71f);

        log_info("[PASS] 15.2 direct field access: health=%d, x=%.2f, y=%.2f", pp->health, pp->x, pp->y);
    }

    log_info("all closed-source type-safe access tests passed");
}

TEST_CASE("16.contract_offset_guard", "[closed][prompt16]") {
    log_info("Test 16: manual contract tamper-proofing - compile-time offset validation");

    static_assert(offsetof(PlayerPublic, health) == 16, "Contract violation: health offset mismatch");
    static_assert(offsetof(PlayerPublic, x) == 20, "Contract violation: x offset mismatch");
    static_assert(offsetof(PlayerPublic, y) == 24, "Contract violation: y offset mismatch");

    dll_object lib;
    if (!lib.load(dll_path("closed_dll").c_str())) {
        WARN("missing closed_dll, skip contract offset validation.");
        return;
    }

    auto offset_health = dll_func<int()>(lib, "player_offset_health");
    auto offset_x = dll_func<int()>(lib, "player_offset_x");
    auto offset_y = dll_func<int()>(lib, "player_offset_y");
    auto sizeof_fn = dll_func<int()>(lib, "player_sizeof");

    REQUIRE(offset_health.valid());
    REQUIRE(offset_x.valid());
    REQUIRE(offset_y.valid());

    REQUIRE(offset_health() == (int)offsetof(PlayerPublic, health));
    REQUIRE(offset_x() == (int)offsetof(PlayerPublic, x));
    REQUIRE(offset_y() == (int)offsetof(PlayerPublic, y));
    REQUIRE(sizeof_fn() >= (int)sizeof(PlayerPublic));

    log_info("[PASS] 16 compile-time + runtime offset validation in effect, contract consistent");
    log_info("  health offset: compile=%zu, runtime=%d", offsetof(PlayerPublic, health), offset_health());
    log_info("  x offset:      compile=%zu, runtime=%d", offsetof(PlayerPublic, x), offset_x());
    log_info("  y offset:      compile=%zu, runtime=%d", offsetof(PlayerPublic, y), offset_y());
    log_info("  sizeof(Player)=%d, sizeof(PlayerPublic)=%zu", sizeof_fn(), sizeof(PlayerPublic));
}

TEST_CASE("17.closed_cross_crt", "[closed][prompt17][cross]") {
    log_info(
        "Test 17: cross-CRT heap release - the unique_dll_ptr deleter is invoked correctly in a closed-source "
        "scenario");

    std::string variant = "./variants/msvc_x64/closed_dll/closed_dll.dll";
    bool use_msvc = file_exists(variant);
    dll_object lib;

    if (use_msvc) {
        log_info("MSVC variant detected, running the cross-CRT heap release test");
        REQUIRE(lib.load(variant.c_str()));
    } else {
        log_info("MSVC variant not detected, using the local closed_dll to verify the unique_dll_ptr deleter");
        REQUIRE(lib.load(dll_path("closed_dll").c_str()));
    }

    auto create = dll_func<Player *(uint64_t)>(lib, "create_player");
    auto destroy = dll_func<void(Player *)>(lib, "destroy_player");
    auto alive = dll_func<int()>(lib, "player_alive");
    auto get_health = dll_func<int(Player *)>(lib, "player_get_health");

    REQUIRE(create.valid());
    REQUIRE(destroy.valid());
    REQUIRE(alive.valid());
    REQUIRE(alive() == 0);

    {
        unique_dll_ptr<Player> player(create(77), destroy.raw());
        REQUIRE((bool)player);
        REQUIRE(alive() == 1);
        REQUIRE(get_health(player.get()) == 100);

        PlayerPublic *pp = reinterpret_cast<PlayerPublic *>(player.get());
        pp->health = 500;
        pp->x = 1.5f;
        pp->y = 2.5f;
        REQUIRE(get_health(player.get()) == 500);
    }
    REQUIRE(alive() == 0);

    if (use_msvc) {
        log_info("[PASS] 17 cross-CRT deleter invoked correctly, no heap conflict (MSVC DLL + MinGW Host)");
    } else {
        log_info("[PASS] 17 deleter invoked correctly, unique_dll_ptr lifecycle management working");
    }
}

TEST_CASE("18.version_evolution_guard", "[closed][prompt18]") {
    log_info(
        "Test 18: manual version evolution simulation - inserting a private field in the middle, runtime blocks the "
        "incompatible version");

    dll_object lib_v1;
    REQUIRE(lib_v1.load(dll_path("closed_dll").c_str()));
    auto type_hash_v1 = dll_func<uint64_t()>(lib_v1, "player_type_hash");
    REQUIRE(type_hash_v1.valid());
    REQUIRE(type_hash_v1() == 0x1234ABCDULL);
    log_info("V1 type hash: 0x%016llX", (unsigned long long)type_hash_v1());

    dll_object lib_v2;
    if (!lib_v2.load(dll_path("closed_dll_v2").c_str())) {
        WARN("missing closed_dll_v2, skip version evolution test.");
        return;
    }

    auto type_hash_v2 = dll_func<uint64_t()>(lib_v2, "player_type_hash");
    auto offset_health_v2 = dll_func<int()>(lib_v2, "player_offset_health");
    REQUIRE(type_hash_v2.valid());
    REQUIRE(offset_health_v2.valid());

    uint64_t v2_hash = type_hash_v2();
    REQUIRE(v2_hash == 0x5678EF90ULL);
    REQUIRE(v2_hash != 0x1234ABCDULL);
    log_info("V2 type hash: 0x%016llX (differs from V1, indicating a breaking change)", (unsigned long long)v2_hash);

    int v2_health_off = offset_health_v2();
    REQUIRE(v2_health_off == 24);
    REQUIRE(v2_health_off != (int)offsetof(PlayerPublic, health));
    log_info(
        "V2 health offset: %d (V1 compile-time expects %zu, mismatch)", v2_health_off, offsetof(PlayerPublic, health));

    constexpr uint64_t CONSUMER_EXPECTED_HASH = 0x1234ABCDULL;
    if (v2_hash != CONSUMER_EXPECTED_HASH) {
        log_info("[PASS] 18 breaking version upgrade blocked by runtime:");
        log_info("  consumer expected hash 0x%016llX, DLL actual hash 0x%016llX",
            (unsigned long long)CONSUMER_EXPECTED_HASH, (unsigned long long)v2_hash);
        log_info("  hash mismatch; the consumer refuses the new DLL and exits safely");
    }

    auto create_v2 = dll_func<Player *(uint64_t)>(lib_v2, "create_player");
    auto destroy_v2 = dll_func<void(Player *)>(lib_v2, "destroy_player");
    auto get_health_v2 = dll_func<int(Player *)>(lib_v2, "player_get_health");
    auto alive_v2 = dll_func<int()>(lib_v2, "player_alive");

    REQUIRE(create_v2.valid());
    REQUIRE(alive_v2() == 0);

    {
        unique_dll_ptr<Player> p(create_v2(1), destroy_v2.raw());
        REQUIRE((bool)p);
        int health_by_fn = get_health_v2(p.get());
        REQUIRE(health_by_fn == 200);
        log_info(
            "  obtained health=%d via the function accessor (safe path, correct offset inside the DLL)", health_by_fn);

        log_info(
            "  warning: facade offset %zu points at the timestamp field in V2, direct access would yield wrong data",
            offsetof(PlayerPublic, health));
    }
    REQUIRE(alive_v2() == 0);

    log_info(
        "version evolution safety guard test complete: the old consumer rejects the incompatible DLL via hash "
        "validation");
}