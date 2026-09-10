#include "test_common.hpp"

TEST_CASE("1.basic_math_linear_scan", "[basic][prompt1]") {
    log_info(
        "Test 1: load math_dll, resolve add/multiply via linear scan of the mics table and call them through "
        "integer handles");
    skl::abix::dll_object lib;
    REQUIRE(lib.load(dll_path("math_dll").c_str()));
    REQUIRE(lib.is_loaded());
    log_info("math_dll loaded, resolving add / multiply");

    auto add = skl::abix::dll_func<int(int, int)>(lib, "add");
    REQUIRE(add.valid());
    REQUIRE(add.handle_id() == 0u);
    REQUIRE(add(2, 3) == 5);
    REQUIRE(add(-5, 5) == 0);
    log_info("add(2,3)=%d, add(-5,5)=%d passed, handle ID=0", add(2, 3), add(-5, 5));

    auto mul = skl::abix::dll_func<double(double, double)>(lib, "multiply");
    REQUIRE(mul.valid());
    REQUIRE(mul(1.5, 4.0) == 6.0);
    log_info("multiply(1.5, 4.0)=%.1f passed", mul(1.5, 4.0));

    const skl::abix::table *t = lib.get_table();
    REQUIRE(t->magic == skl::abix::SKL_ABIX_TABLE_MAGIC);
    static_assert(std::is_trivial_v<skl::abix::entry>);
    static_assert(std::is_trivial_v<skl::abix::table>);

    skl::abix::index_t idx;
    skl::abix::hash_index empty_idx;
    REQUIRE(find_index(*t, empty_idx, "add", skl::abix::fn_sig<int(int, int)>::value, 0, idx) == skl::abix::lookup_result::ok);
    log_info("mics table is plain POD, linear scan find_index(add) hit at index %u", (unsigned)idx);
}

TEST_CASE("2.cross_compiler_variant", "[cross][prompt2]") {
    log_info(
        "Test 2: the same function name yields consistent results across g++/clang/MSVC builds; only C types cross the "
        "boundary");
    for (const char *dir :
        {"./", "./variants/tool_x/version_dll", "./variants/tool_y/version_dll", "./variants/msvc_x64/version_dll"}) {
        std::string full = std::string(dir) +
                           "/version_dll" + SKL_ABIX_DLL_SUFFIX;
        if (!file_exists(full)) {
            log_info("missing cross-compiler variant %s (built in %s)", full.c_str(), dir);
            continue;
        }
        skl::abix::dll_object lib;
        REQUIRE(lib.load(full.c_str()));
        auto get_tag = skl::abix::dll_func<const char *()>(lib, "get_build_tag");
        auto sum = skl::abix::dll_func<int(int, int)>(lib, "get_int_sum");
        auto mul = skl::abix::dll_func<double(double, double)>(lib, "get_double_mul");
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
    skl::abix::dll_object lib;
    REQUIRE(lib.load(dll_path("sigcheck_dll").c_str()));

    auto ok = skl::abix::dll_func<void(int)>(lib, "process");
    REQUIRE(ok.valid());
    ok(42);
    log_info("resolved process with the correct signature void(int) and called process(42) successfully");

    auto bad = skl::abix::dll_func<void(double)>(lib, "process");
    REQUIRE(!bad.valid());
    REQUIRE(skl::abix::last_error() == skl::abix::call_error::sig_mismatch);
    log_info("resolving process with the wrong signature void(double) rejected (sig_mismatch=%d)",
        (int)skl::abix::call_error::sig_mismatch);
    bad(1.0);
    REQUIRE(skl::abix::last_error() == skl::abix::call_error::table_changed);
    log_info(
        "calling an invalid handle does not crash and sets the table_changed error code; no silent int->double "
        "conversion");

    skl::abix::index_t idx;
    skl::abix::hash_index empty_idx;
    auto r = find_index(*lib.get_table(), empty_idx, "process", skl::abix::fn_sig<void(double)>::value, 0, idx);
    REQUIRE(r == skl::abix::lookup_result::sig_mismatch);
    log_info("find_index also returns sig_mismatch for a mismatched signature");
}