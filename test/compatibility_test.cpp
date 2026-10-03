#include <catch2/catch_test_macros.hpp>

#include "ABIX/Model/Compat.h"

TEST_CASE("canonical type, layout and signature hashes are domain separated") {
    constexpr uint8_t name[] = {'F', 'o', 'o'};
    constexpr auto type = skl::abix::model::type_hash(name, sizeof(name));
    constexpr skl::abix::model::TypeLayout layout{
        16, 8, 0, 0, {0, 0}
    };
    constexpr auto first_layout = skl::abix::model::layout_hash(type, layout);
    constexpr auto second_layout = skl::abix::model::layout_hash({0x10, 0}, layout);
    constexpr auto signature = skl::abix::model::signature_hash(type, nullptr, 0, 0);

    REQUIRE(!skl::abix::model::same_hash(type, first_layout));
    REQUIRE(!skl::abix::model::same_hash(first_layout, second_layout));
    REQUIRE(!skl::abix::model::same_hash(type, signature));
}

TEST_CASE("compatibility distinguishes identity, layout and mapped changes") {
    using namespace skl::abix;
    constexpr model::TypeId first{1, 2};
    constexpr model::TypeId second{3, 4};
    constexpr model::TypeLayout same{
        8, 8, 0, 0, {5, 6}
    };
    constexpr model::TypeLayout changed{
        16, 8, 0, 0, {7, 8}
    };

    REQUIRE(model::compare(first, same, first, same) == model::CompatResult::identical);
    REQUIRE(model::compare(first, same, second, same) == model::CompatResult::layout_compatible);
    REQUIRE(model::compare(first, changed, second, same) == model::CompatResult::incompatible);
    REQUIRE(model::compare(first, changed, second, same, true) == model::CompatResult::map_compatible);
}
