#include <catch2/catch_test_macros.hpp>

#include "ABIX/Model/Compat.h"

TEST_CASE("type, layout and ABI signature hashes are domain separated") {
    constexpr auto type = skl::abix::model::canonical_type_id("Foo");
    constexpr skl::abix::model::LayoutContext context{0, 0, 0, 0};
    constexpr skl::abix::model::TypeLayout layout{
        16, 8, 0, 0, {0, 0}
    };
    constexpr auto first_layout = skl::abix::model::canonical_layout_hash(context, type, layout);
    constexpr auto second_layout = skl::abix::model::canonical_layout_hash(context, {0x10, 0}, layout);
    constexpr auto signature = skl::abix::model::abi_signature_hash(type, nullptr, 0, 0);

    REQUIRE(!skl::abix::model::same_hash(type, first_layout));
    REQUIRE(!skl::abix::model::same_hash(first_layout, second_layout));
    REQUIRE(!skl::abix::model::same_hash(type, signature));
    REQUIRE(!skl::abix::model::same_hash(first_layout, signature));
}

TEST_CASE("layout and ABI signature digests react to their own inputs") {
    constexpr auto type = skl::abix::model::canonical_type_id("Foo");
    constexpr skl::abix::model::LayoutContext context{0, 0, 0, 0};
    constexpr skl::abix::model::TypeLayout layout{
        16, 8, 0, 0, {0, 0}
    };
    constexpr skl::abix::model::TypeLayout wider{
        32, 8, 0, 0, {0, 0}
    };
    REQUIRE(!skl::abix::model::same_hash(skl::abix::model::canonical_layout_hash(context, type, layout),
        skl::abix::model::canonical_layout_hash(context, type, wider)));

    constexpr skl::abix::model::TypeId parameter{0x11, 0x22};
    constexpr auto cdecl_sig = skl::abix::model::abi_signature_hash(type, &parameter, 1, 1);
    constexpr auto stdcall_sig = skl::abix::model::abi_signature_hash(type, &parameter, 1, 2);
    REQUIRE(!skl::abix::model::same_hash(cdecl_sig, stdcall_sig));
}

TEST_CASE("canonical layout encoding is deterministic, name independent and target aware") {
    using namespace skl::abix;
    constexpr model::LayoutContext context{1, 1, 1, 1};
    constexpr model::TypeId type{0x11, 0x22};
    constexpr model::TypeLayout layout{
        8, 4, 0, 2, {0, 0}
    };
    constexpr model::Field fields[] = {
        {0, {0x33, 0x44}, 0, 0, 0, 0},
        {0, {0x55, 0x66}, 4, 0, 0, 0},
    };
    constexpr model::Field renamed[] = {
        {0x100, {0x33, 0x44}, 0, 0, 0, 0},
        {0x200, {0x55, 0x66}, 4, 0, 0, 0},
    };
    constexpr auto baseline = model::canonical_layout_hash(context, type, layout, fields);
    REQUIRE(model::same_hash(baseline, model::canonical_layout_hash(context, type, layout, renamed)));

    constexpr model::Field other_visibility[] = {
        {0, {0x33, 0x44}, 0,       0, 0, 0},
        {0, {0x55, 0x66}, 4, 2u << 8, 0, 0},
    };
    REQUIRE(model::same_hash(baseline, model::canonical_layout_hash(context, type, layout, other_visibility)));

    uint8_t buffer[128] = {};
    const size_t encoded = model::encode_canonical_layout(context, type, layout, fields, buffer, sizeof(buffer));
    REQUIRE(encoded == 54 + 2 * 24);
    REQUIRE(model::same_hash(baseline,
        util::canonical_hash(reinterpret_cast<const char *>(buffer), encoded, model::canonical_layout_domain)));

    constexpr model::LayoutContext other_target{2, 1, 1, 1};
    REQUIRE(!model::same_hash(baseline, model::canonical_layout_hash(other_target, type, layout, fields)));

    constexpr model::Field moved[] = {
        {0, {0x33, 0x44}, 0, 0, 0, 0},
        {0, {0x55, 0x66}, 8, 0, 0, 0},
    };
    REQUIRE(!model::same_hash(baseline, model::canonical_layout_hash(context, type, layout, moved)));
}

TEST_CASE("CLE v1 samples: nested, empty, bitfield, inheritance, union, array and targets") {
    using namespace skl::abix;
    constexpr model::LayoutContext target_a{1, 1, 1, 1};
    constexpr model::LayoutContext target_b{2, 2, 2, 2};
    constexpr model::TypeId record{0x10, 0x20};
    constexpr model::TypeId nested{0x30, 0x40};
    constexpr model::TypeId base{0x50, 0x60};
    constexpr model::TypeId element_a{0x70, 0x80};
    constexpr model::TypeId element_b{0x90, 0xA0};
    constexpr uint32_t bitfield = 1u << 0;
    constexpr uint32_t base_flag = 1u << 1;
    constexpr uint32_t visibility_private = 2u << 8;

    // Must be equal: an empty record is a function of TypeID + target + size/align.
    constexpr model::TypeLayout empty{
        1, 1, 0, 0, {0, 0}
    };
    constexpr auto empty_hash = model::canonical_layout_hash(target_a, record, empty);
    REQUIRE(model::same_hash(empty_hash, model::canonical_layout_hash(target_a, record, empty)));
    // Must differ: a different TypeID, or a different target ABI.
    REQUIRE(!model::same_hash(empty_hash, model::canonical_layout_hash(target_a, {0x11, 0x21}, empty)));
    REQUIRE(!model::same_hash(empty_hash, model::canonical_layout_hash(target_b, record, empty)));

    // Nested record: the field's TypeID is part of the outer layout.
    constexpr model::TypeLayout nested_layout{
        8, 8, 0, 1, {0, 0}
    };
    constexpr model::Field nested_field[] = {
        {0, nested, 0, 0, 0, 0}
    };
    constexpr model::Field other_nested[] = {
        {0, element_a, 0, 0, 0, 0}
    };
    constexpr auto nested_hash = model::canonical_layout_hash(target_a, record, nested_layout, nested_field);
    REQUIRE(model::same_hash(nested_hash, model::canonical_layout_hash(target_a, record, nested_layout, nested_field)));
    REQUIRE(
        !model::same_hash(nested_hash, model::canonical_layout_hash(target_a, record, nested_layout, other_nested)));

    // Bitfields: the packed bit width is layout; visibility is not.
    constexpr model::TypeLayout bit_layout{
        4, 4, 0, 1, {0, 0}
    };
    constexpr model::Field bit4[] = {
        {0, element_a, 0, bitfield | (4u << 24), 0, 4}
    };
    constexpr model::Field bit8[] = {
        {0, element_a, 0, bitfield | (8u << 24), 0, 8}
    };
    constexpr model::Field bit8_private[] = {
        {0, element_a, 0, bitfield | (8u << 24) | visibility_private, 0, 8}
    };
    constexpr auto bit4_hash = model::canonical_layout_hash(target_a, record, bit_layout, bit4);
    constexpr auto bit8_hash = model::canonical_layout_hash(target_a, record, bit_layout, bit8);
    REQUIRE(!model::same_hash(bit4_hash, bit8_hash));
    REQUIRE(model::same_hash(bit8_hash, model::canonical_layout_hash(target_a, record, bit_layout, bit8_private)));

    // Inheritance: a base entry is marked and its offset is layout.
    constexpr model::TypeLayout derived{
        16, 8, 0, 1, {0, 0}
    };
    constexpr model::Field base_at_0[] = {
        {0, base, 0, base_flag, 0, 0}
    };
    constexpr model::Field base_at_8[] = {
        {0, base, 8, base_flag, 0, 0}
    };
    REQUIRE(!model::same_hash(model::canonical_layout_hash(target_a, record, derived, base_at_0),
        model::canonical_layout_hash(target_a, record, derived, base_at_8)));

    // Union: two members sharing offset 0; moving one member is a different layout.
    constexpr model::TypeLayout union_layout{
        8, 8, 0, 2, {0, 0}
    };
    constexpr model::Field union_fields[] = {
        {0, element_a, 0, 0, 0, 0},
        {0, element_b, 0, 0, 0, 0}
    };
    constexpr model::Field struct_fields[] = {
        {0, element_a, 0, 0, 0, 0},
        {0, element_b, 4, 0, 0, 0}
    };
    REQUIRE(!model::same_hash(model::canonical_layout_hash(target_a, record, union_layout, union_fields),
        model::canonical_layout_hash(target_a, record, union_layout, struct_fields)));

    // Array: the array TypeID and physical size/align describe the element
    // identity; an equal descriptor is equal, a different one is not.
    constexpr model::TypeId array_a{0x01, 0x02};
    constexpr model::TypeId array_b{0x03, 0x04};
    constexpr model::TypeLayout array_layout{
        32, 8, 0, 0, {0, 0}
    };
    REQUIRE(model::same_hash(model::canonical_layout_hash(target_a, array_a, array_layout),
        model::canonical_layout_hash(target_a, array_a, array_layout)));
    REQUIRE(!model::same_hash(model::canonical_layout_hash(target_a, array_a, array_layout),
        model::canonical_layout_hash(target_a, array_b, array_layout)));
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
