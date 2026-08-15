#include "test_common.hpp"

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