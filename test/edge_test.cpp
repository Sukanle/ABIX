#include "test_common.hpp"

TEST_CASE("10.edge_handling", "[edge][prompt10]") {
    log_info(
        "Test 10: edge cases and error handling - not found / call after unload / ref-count rejects unload / calling "
        "convention mismatch");
    {
        skl::abix::dll::Object lib;
        REQUIRE(lib.load(dll_path("edge_dll").c_str()));
        auto missing = skl::abix::dll::Function<int(int, int)>(lib, "no_such_fn");
        REQUIRE(!missing.valid());
        REQUIRE(skl::abix::dll::last_error() == skl::abix::dll::CallError::not_found);
        log_info("a) name no_such_fn not found in the table -> valid()=false, error code=not_found(%d)",
            (int)skl::abix::dll::CallError::not_found);
    }

    {
        skl::abix::dll::Object *lib = new skl::abix::dll::Object;
        REQUIRE(lib->load(dll_path("edge_dll").c_str()));
        auto fn = skl::abix::dll::Function<int(int, int)>(*lib, "compute");
        REQUIRE(fn.valid());
        lib->force_unload();
        REQUIRE(!fn.valid());
        int r = fn(3, 2);
        REQUIRE(skl::abix::dll::last_error() == skl::abix::dll::CallError::not_loaded);
        log_info(
            "b) calling the stale compute handle after unload -> no crash, returns default, error code=not_loaded(%d)",
            (int)skl::abix::dll::CallError::not_loaded);
        (void)r;
        delete lib;
        REQUIRE(fn(1, 2) == 0);   // handle outlives the Object: shared Control keeps it safe
    }

    {
        skl::abix::dll::Object lib;
        REQUIRE(lib.load(dll_path("edge_dll").c_str()));
        auto fn = skl::abix::dll::Function<int(int, int)>(lib, "compute");
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
        skl::abix::dll::Object lib;
        REQUIRE(lib.load(dll_path("edge_stdcall_dll").c_str()));
        auto wrong = skl::abix::dll::Function<int(int, int)>(lib, "compute");
        REQUIRE(!wrong.valid());
        REQUIRE(skl::abix::dll::last_error() == skl::abix::dll::CallError::sig_mismatch);
        log_info("c) requesting the stdcall-registered compute with cdecl rejected (sig_mismatch)");
        auto right = skl::abix::dll::Function<int(int, int), SKL_ABIX_CCPICK(Stdcall)>(lib, "compute");
        REQUIRE(right.valid());
        REQUIRE(right(6, 7) == 42);
        log_info("  stdcall request matched, compute(6,7)=42");
    }
}
