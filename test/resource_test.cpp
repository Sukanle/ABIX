#include "test_common.hpp"

TEST_CASE("4.unique_resource_takeover", "[resource][prompt4]") {
    log_info(
        "Test 4: exclusive resource takeover across DLLs - both unique_dll_ptr / unique_ptr invoke the DLL release "
        "function");
    skl::abix::dll_object lib;
    REQUIRE(lib.load(dll_path("resource_dll").c_str()));
    log_info("resource_dll loaded, preparing resource lifecycle tests");

    {
        auto alive = skl::abix::dll_func<int()>(lib, "get_resource_alive");
        auto create = skl::abix::dll_func<Resource *(int)>(lib, "create_resource");
        auto destroy = skl::abix::dll_func<void(Resource *)>(lib, "destroy_resource");
        auto rid = skl::abix::dll_func<int(Resource *)>(lib, "resource_id");
        auto rpay = skl::abix::dll_func<int(Resource *, int)>(lib, "resource_payload");
        REQUIRE(create.valid());
        REQUIRE(destroy.valid());

        REQUIRE(alive() == 0);
        log_info("initial alive resource count = %d", alive());

        {
            skl::abix::unique_dll_ptr<Resource> h(create(7), destroy.raw());
            REQUIRE((bool)h);
            REQUIRE(rid(h.get()) == 7);
            REQUIRE(rpay(h.get(), 3) == 3);
            log_info("method A: unique_dll_ptr holds resource id=%d, payload[3]=%d", rid(h.get()), rpay(h.get(), 3));
        }
        REQUIRE(alive() == 0);
        log_info("after leaving scope, the DLL destroy_resource was called and the alive count dropped to zero");

        {
            skl::abix::fn_deleter<Resource> dl(destroy.raw());
            ::std::unique_ptr<Resource, skl::abix::fn_deleter<Resource>> up(create(3), dl);
            REQUIRE(rid(up.get()) == 3);
            log_info("method B: unique_ptr<Resource, fn_deleter> holds resource id=%d", rid(up.get()));
        }
        REQUIRE(alive() == 0);
        log_info("unique_ptr called the DLL release function via fn_deleter; the alive count dropped to zero");

        {
            skl::abix::unique_dll_ptr<Resource> h(create(1), destroy.raw());
            REQUIRE(lib.ref_count() >= 1);
            REQUIRE(lib.unload() == false);
            REQUIRE(skl::abix::last_error() == skl::abix::call_error::stale_handle);
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
    skl::abix::dll_object lib;
    REQUIRE(lib.load(dll_path("resource_dll").c_str()));

    auto alive = skl::abix::dll_func<int()>(lib, "get_config_alive");
    auto area = skl::abix::dll_func<int(Config *)>(lib, "config_area");
    auto create_shared = skl::abix::dll_func<skl::abix::ref_dll_ptr<Config>(int, int)>(lib, "create_shared_config");
    REQUIRE(create_shared.valid());

    {
        skl::abix::ref_dll_ptr<Config> a = create_shared(3, 4);
        REQUIRE(a.use_count() == 1);
        REQUIRE(area(a.get()) == 12);
        log_info("first handle a created, ref-count=1, config_area=%d", area(a.get()));

        skl::abix::ref_dll_ptr<Config> b = a;
        REQUIRE(a.use_count() == 2);
        REQUIRE(b.use_count() == 2);
        log_info("after copying b=a ref-count=2");

        skl::abix::ref_dll_ptr<Config> c = b;
        REQUIRE(a.use_count() == 3);
        log_info("after copying c=b ref-count=3");
        {
            skl::abix::ref_dll_ptr<Config> d = c;
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

TEST_CASE("11.char_string_copy", "[resource][prompt11][charcopy]") {
    log_info(
        "Test 11: character copy resource - strdup_copy allocates in the DLL, the host reads, then the DLL releases");
    skl::abix::dll_object lib;
    REQUIRE(lib.load(dll_path("resource_dll").c_str()));

    auto strdup = skl::abix::dll_func<char *(const char *, int *)>(lib, "strdup_copy");
    auto freed = skl::abix::dll_func<void(char *)>(lib, "string_destroy");
    REQUIRE(strdup.valid());
    REQUIRE(freed.valid());
    log_info("resolved strdup_copy / string_destroy, starting the character copy test");

    const char *src = "Cross DLL character copy - hello reflection table";
    int len = -1;
    char *dst = strdup(src, &len);
    REQUIRE(dst != nullptr);
    REQUIRE(len == static_cast<int>(strlen(src)));
    REQUIRE(strcmp(dst, src) == 0);
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
    skl::abix::dll_object lib;
    REQUIRE(lib.load(dll_path("resource_dll").c_str()));

    auto alive = skl::abix::dll_func<int()>(lib, "socket_alive");
    auto open = skl::abix::dll_func<Socket *(const char *, int)>(lib, "socket_open");
    auto close = skl::abix::dll_func<void(Socket *)>(lib, "socket_close");
    auto isopen = skl::abix::dll_func<int(const Socket *)>(lib, "socket_is_open");
    auto fd = skl::abix::dll_func<int(const Socket *)>(lib, "socket_fd");
    auto send_ = skl::abix::dll_func<int(Socket *, const char *)>(lib, "socket_send");
    auto recv_ = skl::abix::dll_func<int(Socket *, char *, int)>(lib, "socket_recv");
    REQUIRE(open.valid());
    REQUIRE(close.valid());
    REQUIRE(send_.valid());
    REQUIRE(recv_.valid());

    REQUIRE(alive() == 0);
    log_info("initial state: socket_alive=0 (no connections yet)");

    {
        skl::abix::unique_dll_ptr<Socket> s(open("127.0.0.1", 8'080), close.raw());
        REQUIRE((bool)s);
        REQUIRE(isopen(s.get()) == 1);
        REQUIRE(fd(s.get()) >= 0);
        REQUIRE(alive() == 1);
        log_info("opened a Socket (fd=%d, http://127.0.0.1:8080), socket_alive=1", fd(s.get()));

        const char *payload = "GET /api HTTP/1.1";
        int n = send_(s.get(), payload);
        REQUIRE(n == static_cast<int>(strlen(payload)));
        log_info("socket_send sent [%s] successfully, returned byte count=%d", payload, n);

        char buf[128];
        memset(buf, 0, sizeof(buf));
        int got = recv_(s.get(), buf, (int)sizeof(buf) - 1);
        REQUIRE(got == static_cast<int>(strlen(payload)));
        REQUIRE(strcmp(buf, payload) == 0);
        log_info("socket_recv read the receive buffer, got [%s] (byte count=%d)", buf, got);
    }
    REQUIRE(alive() == 0);
    log_info("after leaving scope unique_dll_ptr automatically called socket_close, socket_alive=0");
    log_info("Socket resource lifecycle test complete: open -> send -> recv -> close all correct");
}

TEST_CASE("13.cross_crt_msvc_resource", "[cross][prompt13][msvc][resource]") {
#ifdef SKL_ABIX_WINDOWS
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
    skl::abix::dll_object lib;
    REQUIRE(lib.load(variant.c_str()));
    log_info("loaded the MSVC-compiled resource_dll (cross-CRT resource lifecycle)");

    {
        auto strdup = skl::abix::dll_func<char *(const char *, int *)>(lib, "strdup_copy");
        auto freed = skl::abix::dll_func<void(char *)>(lib, "string_destroy");
        REQUIRE(strdup.valid());
        REQUIRE(freed.valid());
        int len;
        char *s = strdup("Cross DLL character copy - hello reflection table", &len);
        REQUIRE(s != nullptr);
        REQUIRE(strcmp(s, "Cross DLL character copy - hello reflection table") == 0);
        log_info(" [MSVC] strdup_copy cross-CRT copy of [%s] (%d bytes) succeeded", s, len);
        freed(s);
        log_info(" [MSVC] string_destroy release succeeded, no heap conflict");
    }

    {
        auto alive = skl::abix::dll_func<int()>(lib, "get_resource_alive");
        auto create = skl::abix::dll_func<Resource *(int)>(lib, "create_resource");
        auto destroy = skl::abix::dll_func<void(Resource *)>(lib, "destroy_resource");
        auto rid = skl::abix::dll_func<int(Resource *)>(lib, "resource_id");
        REQUIRE(create.valid());
        REQUIRE(destroy.valid());
        REQUIRE(alive() == 0);
        {
            skl::abix::unique_dll_ptr<Resource> h(create(9), destroy.raw());
            REQUIRE(rid(h.get()) == 9);
            REQUIRE(alive() == 1);
        }
        REQUIRE(alive() == 0);
        log_info(" [MSVC] Resource created/released across CRT via unique_dll_ptr, alive count dropped to zero");
    }

    {
        auto alive = skl::abix::dll_func<int()>(lib, "socket_alive");
        auto open = skl::abix::dll_func<Socket *(const char *, int)>(lib, "socket_open");
        auto close = skl::abix::dll_func<void(Socket *)>(lib, "socket_close");
        auto send_ = skl::abix::dll_func<int(Socket *, const char *)>(lib, "socket_send");
        auto recv_ = skl::abix::dll_func<int(Socket *, char *, int)>(lib, "socket_recv");
        REQUIRE(open.valid());
        {
            skl::abix::unique_dll_ptr<Socket> s(open("10.0.0.1", 443), close.raw());
            REQUIRE(alive() == 1);
            send_(s.get(), "PING");
            char buf[16];
            memset(buf, 0, sizeof(buf));
            int got = recv_(s.get(), buf, (int)sizeof(buf) - 1);
            REQUIRE(got == static_cast<int>(strlen("PING")));
            REQUIRE(strcmp(buf, "PING") == 0);
        }
        REQUIRE(alive() == 0);
        log_info(" [MSVC] Socket open->send->recv->close across CRT, alive count dropped to zero");
    }

    {
        auto alive = skl::abix::dll_func<int()>(lib, "get_config_alive");
        auto create = skl::abix::dll_func<skl::abix::ref_dll_ptr<Config>(int, int)>(lib, "create_shared_config");
        auto area = skl::abix::dll_func<int(Config *)>(lib, "config_area");
        REQUIRE(create.valid());
        {
            skl::abix::ref_dll_ptr<Config> a = create(5, 2);
            REQUIRE(area(a.get()) == 10);
            skl::abix::ref_dll_ptr<Config> b = a;
            REQUIRE(a.use_count() == 2);
        }
        REQUIRE(alive() == 0);
        log_info(" [MSVC] ref_dll_ptr ref-count dropped to zero across CRT and released correctly");
    }
    log_info("MinGW host loaded the MSVC resource_dll; all resource lifecycles are correct across CRT");
#else
    log_info("Operation System is not Windows, skip cross-CRT msvc resource test on this platform");
#endif
}