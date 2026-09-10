#include "test_common.hpp"

TEST_CASE("21.rcu_timeout_config", "[config][prompt21]") {
    log_info("Test 21: RCUTimeoutConfig — default values, explicit construction, and frame-based override");

    SECTION("21a.default_construction") {
        skl::abix::RCUTimeoutConfig cfg;
        REQUIRE(cfg.timeout_ms == ABIX_RCU_TIMEOUT_MS);
        REQUIRE(cfg.timeout_frames == ABIX_RCU_TIMEOUT_FRAMES_DEFAULT);
        log_info("default config: timeout_ms=%llu, timeout_frames=%llu", (unsigned long long)cfg.timeout_ms,
            (unsigned long long)cfg.timeout_frames);
    }

    SECTION("21b.explicit_ms_only") {
        skl::abix::RCUTimeoutConfig cfg(3'000);
        REQUIRE(cfg.timeout_ms == 3'000);
        REQUIRE(cfg.timeout_frames == ABIX_RCU_TIMEOUT_FRAMES_DEFAULT);
        log_info("explicit ms=3000: timeout_ms=%llu, timeout_frames=%llu", (unsigned long long)cfg.timeout_ms,
            (unsigned long long)cfg.timeout_frames);
    }

    SECTION("21c.both_ms_and_frames") {
        skl::abix::RCUTimeoutConfig cfg(5'000, 300);
        REQUIRE(cfg.timeout_ms == 5'000);
        REQUIRE(cfg.timeout_frames == 300);
        log_info("game engine config: timeout_ms=%llu, timeout_frames=%llu", (unsigned long long)cfg.timeout_ms,
            (unsigned long long)cfg.timeout_frames);
    }

    SECTION("21d.constexpr_construction") {
        constexpr skl::abix::RCUTimeoutConfig cfg(10'000, 600);
        static_assert(cfg.timeout_ms == 10'000, "constexpr timeout_ms mismatch");
        static_assert(cfg.timeout_frames == 600, "constexpr timeout_frames mismatch");
        log_info("constexpr config: timeout_ms=%llu, timeout_frames=%llu", (unsigned long long)cfg.timeout_ms,
            (unsigned long long)cfg.timeout_frames);
    }
}

TEST_CASE("22.dll_object_rcu_config", "[config][prompt22]") {
    log_info("Test 22: dll_object constructed with RCUTimeoutConfig");

    SECTION("22a.default_constructor") {
        skl::abix::dll_object lib;
        REQUIRE(lib.timeout_policy() == skl::abix::RCUTimeoutPolicy::Safe);
        log_info("default dll_object has Safe timeout policy");
    }

    SECTION("22b.config_constructor") {
        skl::abix::dll_object lib(skl::abix::RCUTimeoutConfig{2'000});
        REQUIRE(lib.timeout_policy() == skl::abix::RCUTimeoutPolicy::Safe);
        log_info("dll_object with 2000ms config has Safe timeout policy");
    }

    SECTION("22c.frame_config_constructor") {
        skl::abix::dll_object lib(skl::abix::RCUTimeoutConfig{5'000, 300});
        REQUIRE(lib.timeout_policy() == skl::abix::RCUTimeoutPolicy::Safe);
        log_info("dll_object with frame config has Safe timeout policy");
    }
}

TEST_CASE("23.timeout_policy_runtime", "[policy][prompt23]") {
    log_info("Test 23: runtime timeout policy switching");

    skl::abix::dll_object lib;
    REQUIRE(lib.timeout_policy() == skl::abix::RCUTimeoutPolicy::Safe);

    SECTION("23a.switch_to_force_unload") {
        lib.set_timeout_policy(skl::abix::RCUTimeoutPolicy::ForceUnload);
        REQUIRE(lib.timeout_policy() == skl::abix::RCUTimeoutPolicy::ForceUnload);
        log_info("switched to ForceUnload policy");
    }

    SECTION("23b.switch_to_safe") {
        lib.set_timeout_policy(skl::abix::RCUTimeoutPolicy::Safe);
        REQUIRE(lib.timeout_policy() == skl::abix::RCUTimeoutPolicy::Safe);
        log_info("switched back to Safe policy");
    }

    SECTION("23c.switch_to_force_leak") {
        lib.set_timeout_policy(skl::abix::RCUTimeoutPolicy::ForceLeak);
        REQUIRE(lib.timeout_policy() == skl::abix::RCUTimeoutPolicy::ForceLeak);
        log_info("switched to ForceLeak policy");
    }
}