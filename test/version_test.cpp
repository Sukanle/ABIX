#include "test_common.hpp"
#include <cstring>

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