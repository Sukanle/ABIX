#include <catch2/catch_test_macros.hpp>

#include "ABIX/Util/Hash.h"

TEST_CASE("canonical hash is deterministic, domain separated and endian neutral") {
    constexpr char input[] = "Foo";
    constexpr auto type = skl::abix::util::canonical_hash(input, 3, 0x54595045ULL);
    constexpr auto layout = skl::abix::util::canonical_hash(input, 3, 0x4C41594F55545F31ULL);
    constexpr auto again = skl::abix::util::canonical_hash(input, 3, 0x54595045ULL);

    STATIC_REQUIRE(type.lo == 0x9E4865E33F33EF18ULL);
    STATIC_REQUIRE(type.hi == 0xF5973B85E61B6073ULL);
    STATIC_REQUIRE(type == again);
    REQUIRE(type != layout);
}

TEST_CASE("structural digests encode integers big-endian") {
    using skl::abix::util::canonical_hash_begin;
    using skl::abix::util::canonical_hash_byte;
    using skl::abix::util::canonical_hash_u32_be;
    using skl::abix::util::canonical_hash_u64_be;

    auto state = canonical_hash_begin(0x1234);
    state = canonical_hash_u32_be(state, 0x01020304U);
    state = canonical_hash_u64_be(state, 0x0102030405060708ULL);

    auto bytes = canonical_hash_begin(0x1234);
    for (uint8_t byte : {1, 2, 3, 4, 1, 2, 3, 4, 5, 6, 7, 8})
        bytes = canonical_hash_byte(bytes, byte);

    REQUIRE(state.lo == bytes.lo);
    REQUIRE(state.hi == bytes.hi);
}
