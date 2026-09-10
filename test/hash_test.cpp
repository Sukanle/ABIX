#include <catch2/catch_test_macros.hpp>

#include "abix/hash.h"

TEST_CASE("canonical hash values carry algorithm and domain") {
    constexpr uint8_t input[] = {'F', 'o', 'o'};
    constexpr auto type = skl::abix::hash::fnv1a(
        input, sizeof(input), skl::abix::hash::Domain::type_identity);
    constexpr auto layout = skl::abix::hash::fnv1a(
        input, sizeof(input), skl::abix::hash::Domain::layout_identity);

    STATIC_REQUIRE(type.descriptor.algorithm ==
                   skl::abix::hash::Algorithm::fnv1a_dual_lane_reference);
    STATIC_REQUIRE(type.descriptor.digest_bits == 128);
    REQUIRE(!skl::abix::hash::same_value(type, layout));
    REQUIRE(skl::abix::hash::runtime_key(type.digest) != 0);
}

TEST_CASE("hash value set supports algorithm negotiation") {
    constexpr uint8_t input[] = {'F', 'o', 'o'};
    const auto fnv = skl::abix::hash::fnv1a(
        input, sizeof(input), skl::abix::hash::Domain::type_identity);
    auto alternate = fnv;
    alternate.descriptor.algorithm = skl::abix::hash::Algorithm::blake3;
    alternate.descriptor.digest_bits = 128;

    skl::abix::hash::ValueSet<2> values;
    REQUIRE(values.add(fnv));
    REQUIRE(values.add(alternate));
    REQUIRE(values.size() == 2);
    REQUIRE(values.find(alternate.descriptor) != nullptr);
}
