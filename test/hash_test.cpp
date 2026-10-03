#include <catch2/catch_test_macros.hpp>

#include "ABIX/Util/Hash.h"

TEST_CASE("canonical hash values carry algorithm and domain") {
    constexpr uint8_t input[] = {'F', 'o', 'o'};
    constexpr auto type = skl::abix::util::fnv1a(input, sizeof(input), skl::abix::util::Domain::type_identity);
    constexpr auto layout = skl::abix::util::fnv1a(input, sizeof(input), skl::abix::util::Domain::layout_identity);

    STATIC_REQUIRE(type.descriptor.algorithm == skl::abix::util::Algorithm::fnv1a_dual_lane_reference);
    STATIC_REQUIRE(type.descriptor.digest_bits == 128);
    REQUIRE(!skl::abix::util::same_value(type, layout));
    REQUIRE(skl::abix::util::runtime_key(type.digest) != 0);
}

TEST_CASE("hash value set supports algorithm negotiation") {
    constexpr uint8_t input[] = {'F', 'o', 'o'};
    const auto fnv = skl::abix::util::fnv1a(input, sizeof(input), skl::abix::util::Domain::type_identity);
    auto alternate = fnv;
    alternate.descriptor.algorithm = skl::abix::util::Algorithm::blake3;
    alternate.descriptor.digest_bits = 128;

    skl::abix::util::ValueSet<2> values;
    REQUIRE(values.add(fnv));
    REQUIRE(values.add(alternate));
    REQUIRE(values.size() == 2);
    REQUIRE(values.find(alternate.descriptor) != nullptr);
}
