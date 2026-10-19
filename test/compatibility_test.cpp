#include <catch2/catch_test_macros.hpp>

#include "ABIX/Model/Compat.h"

TEST_CASE("type, layout and ABI signature hashes are domain separated") {
    constexpr auto type = skl::abix::model::canonical_type_id("Foo");
    constexpr skl::abix::model::TypeLayout layout{
        16, 8, 0, 0, {0, 0}
    };
    constexpr auto first_layout = skl::abix::model::structural_layout_hash(type, layout);
    constexpr auto second_layout = skl::abix::model::structural_layout_hash({0x10, 0}, layout);
    constexpr auto signature = skl::abix::model::abi_signature_hash(type, nullptr, 0, 0);

    REQUIRE(!skl::abix::model::same_hash(type, first_layout));
    REQUIRE(!skl::abix::model::same_hash(first_layout, second_layout));
    REQUIRE(!skl::abix::model::same_hash(type, signature));
    REQUIRE(!skl::abix::model::same_hash(first_layout, signature));
}

TEST_CASE("layout and ABI signature digests react to their own inputs") {
    constexpr auto type = skl::abix::model::canonical_type_id("Foo");
    constexpr skl::abix::model::TypeLayout layout{
        16, 8, 0, 0, {0, 0}
    };
    constexpr skl::abix::model::TypeLayout wider{
        32, 8, 0, 0, {0, 0}
    };
    REQUIRE(!skl::abix::model::same_hash(
        skl::abix::model::structural_layout_hash(type, layout), skl::abix::model::structural_layout_hash(type, wider)));

    constexpr skl::abix::model::TypeId parameter{0x11, 0x22};
    constexpr auto cdecl_sig = skl::abix::model::abi_signature_hash(type, &parameter, 1, 1);
    constexpr auto stdcall_sig = skl::abix::model::abi_signature_hash(type, &parameter, 1, 2);
    REQUIRE(!skl::abix::model::same_hash(cdecl_sig, stdcall_sig));
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
