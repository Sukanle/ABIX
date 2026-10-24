/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0.
 */
#include <catch2/catch_test_macros.hpp>

#include "SKL/ABIX/Interface/Interface.h"

namespace {

using skl::abix::model::TypeId;
using skl::abix::poly::interface_entry;
using skl::abix::poly::InterfaceId;
using skl::abix::poly::InterfaceMethod;
using skl::abix::poly::InterfaceMethodHash;
using skl::abix::poly::InterfaceRef;
using skl::abix::poly::InterfaceRegistry;
using skl::abix::poly::InterfaceStatus;
using skl::abix::poly::UniqueInterfaceProxy;

// Provider types. They carry no vtable and are never modified: the adapters
// below are the only bridge, so the consumer never reads the object layout.
struct Vec2 {
    int x;
    int y;
};
struct Counter {
    int n;
};

using AxisFn = int (*)(const void *, int);
using ScaleFn = void (*)(void *, int);
using ValueFn = int (*)(const void *);

int vec2_axis(const void *self, int axis) {
    const auto *v = static_cast<const Vec2 *>(self);
    return axis == 0 ? v->x : v->y;
}
void vec2_scale(void *self, int factor) {
    auto *v = static_cast<Vec2 *>(self);
    v->x *= factor;
    v->y *= factor;
}
int counter_value(const void *self) { return static_cast<const Counter *>(self)->n; }

}   // namespace

TEST_CASE("interface dispatch resolves once and then calls natively") {
    constexpr TypeId vec_id{0x11, 0x12};
    constexpr InterfaceId ivec{0x31, 0x32};

    const InterfaceMethod vec_methods[] = {
        {{0xa1, 0x0},  interface_entry(&vec2_axis), 0, 0},
        {{0xa2, 0x0}, interface_entry(&vec2_scale), 1, 0},
    };

    InterfaceRegistry<4> registry;
    REQUIRE(registry.add({ivec, vec_id, vec_methods, 2, 1, 0}) == InterfaceStatus::ok);
    REQUIRE(registry.size() == 1);

    Vec2 v{2, 3};
    InterfaceRef<4> ref;
    REQUIRE(ref.resolve(registry, &v, vec_id, ivec, 1) == InterfaceStatus::ok);
    REQUIRE(ref.valid());
    REQUIRE(ref.interface_id() == ivec);
    REQUIRE(ref.version() == 1);
    REQUIRE(ref.method_count() == 2);
    // resolve() is the only lookup.
    REQUIRE(registry.lookup_count() == 1);

    const auto axis = ref.method<AxisFn>(0);
    const auto scale = ref.method<ScaleFn>(1);
    REQUIRE(axis != nullptr);
    REQUIRE(scale != nullptr);
    CHECK(axis(ref.object(), 0) == 2);
    CHECK(axis(ref.object(), 1) == 3);
    scale(ref.object(), 10);
    CHECK(axis(ref.object(), 0) == 20);
    CHECK(axis(ref.object(), 1) == 30);
    // Every call served from the cached binding: no further registry lookups.
    REQUIRE(registry.lookup_count() == 1);

    // Another object of the same type runs the same cached adapter.
    Vec2 w{7, 8};
    CHECK(axis(&w, 0) == 7);
}

TEST_CASE("interface versions are selected exactly and default to newest") {
    constexpr TypeId counter_id{0x21, 0x22};
    constexpr InterfaceId icount{0x41, 0x42};

    const InterfaceMethod v1_methods[] = {
        {{0xb1, 0x0}, interface_entry(&counter_value), 0, 0},
    };
    const InterfaceMethod v2_methods[] = {
        {{0xb1, 0x0}, interface_entry(&counter_value), 0, 0},
    };

    InterfaceRegistry<4> registry;
    REQUIRE(registry.add({icount, counter_id, v1_methods, 1, 1, 0}) == InterfaceStatus::ok);
    REQUIRE(registry.add({icount, counter_id, v2_methods, 1, 2, 0}) == InterfaceStatus::ok);

    REQUIRE(registry.find(counter_id, icount, 1)->version == 1);
    REQUIRE(registry.find(counter_id, icount, 2)->version == 2);
    // Version 0 means "any version" and selects the newest.
    REQUIRE(registry.find(counter_id, icount, 0)->version == 2);

    uint32_t newest = 0;
    REQUIRE(registry.available(counter_id, icount, newest));
    REQUIRE(newest == 2);

    Counter c{5};
    InterfaceRef<2> ref;
    REQUIRE(ref.resolve(registry, &c, counter_id, icount, 1) == InterfaceStatus::ok);
    REQUIRE(ref.version() == 1);
    // A missing version is a version mismatch, not an unknown interface.
    REQUIRE(ref.resolve(registry, &c, counter_id, icount, 9) == InterfaceStatus::version_mismatch);
    REQUIRE(ref.resolve(registry, &c, counter_id, icount, 0) == InterfaceStatus::ok);
    REQUIRE(ref.version() == 2);

    const auto value = ref.method<ValueFn>(0);
    REQUIRE(value != nullptr);
    CHECK(value(ref.object()) == 5);
}

TEST_CASE("interface registry rejects malformed and conflicting tables") {
    constexpr TypeId vec_id{0x11, 0x12};
    constexpr TypeId counter_id{0x21, 0x22};
    constexpr InterfaceId ivec{0x31, 0x32};

    const InterfaceMethod vec_methods[] = {
        {{0xa1, 0x0}, interface_entry(&vec2_axis), 0, 0},
    };
    InterfaceRegistry<2> registry;

    // Zero interface id, or a method count with no table, is invalid.
    REQUIRE(registry.add({{}, vec_id, vec_methods, 1, 1, 0}) == InterfaceStatus::invalid_table);
    REQUIRE(registry.add({ivec, vec_id, nullptr, 1, 1, 0}) == InterfaceStatus::invalid_table);
    REQUIRE(registry.add({ivec, vec_id, vec_methods, 1, 1, 0}) == InterfaceStatus::ok);
    // A repeated (concrete, interface, version) is a conflict.
    REQUIRE(registry.add({ivec, vec_id, vec_methods, 1, 1, 0}) == InterfaceStatus::duplicate_implementation);
    REQUIRE(registry.add({ivec, counter_id, vec_methods, 1, 1, 0}) == InterfaceStatus::ok);
    REQUIRE(registry.add({
                ivec, TypeId{0x99, 0x99},
                 vec_methods, 1, 1, 0
    })
            == InterfaceStatus::capacity_exceeded);

    Vec2 v{1, 2};
    InterfaceRef<2> ref;
    REQUIRE(ref.resolve(registry, &v, vec_id, InterfaceId{0xde, 0xad}) == InterfaceStatus::interface_not_found);
    REQUIRE(ref.resolve(registry, &v, TypeId{0xde, 0xad}, ivec) == InterfaceStatus::interface_not_found);
    REQUIRE(ref.resolve(registry, nullptr, vec_id, ivec) == InterfaceStatus::invalid_table);

    // A slot above the handle's capacity is refused at binding time.
    const InterfaceMethod wide[] = {
        {{0xc1, 0x0}, interface_entry(&vec2_axis), 5, 0},
    };
    InterfaceRegistry<2> wide_registry;
    REQUIRE(wide_registry.add({ivec, vec_id, wide, 1, 1, 0}) == InterfaceStatus::ok);
    REQUIRE(ref.resolve(wide_registry, &v, vec_id, ivec, 1) == InterfaceStatus::method_capacity_exceeded);
}

TEST_CASE("unbound interface slots are reported rather than guessed") {
    constexpr TypeId vec_id{0x11, 0x12};
    constexpr InterfaceId ivec{0x31, 0x32};

    const InterfaceMethod sparse[] = {
        {{0xd1, 0x0},  interface_entry(&vec2_axis), 0, 0},
        {{0xd3, 0x0}, interface_entry(&vec2_scale), 2, 0},
    };
    InterfaceRegistry<2> registry;
    REQUIRE(registry.add({ivec, vec_id, sparse, 2, 1, 0}) == InterfaceStatus::ok);

    Vec2 v{1, 2};
    InterfaceRef<4> ref;
    REQUIRE(ref.resolve(registry, &v, vec_id, ivec, 1) == InterfaceStatus::ok);
    REQUIRE(ref.method_count() == 3);
    REQUIRE(ref.bound(0));
    REQUIRE_FALSE(ref.bound(1));
    REQUIRE(ref.bound(2));
    REQUIRE(ref.method<AxisFn>(1) == nullptr);
}

TEST_CASE("a resolved method binding verifies its recorded signature") {
    constexpr TypeId vec_id{0x11, 0x12};
    constexpr InterfaceId ivec{0x31, 0x32};
    constexpr InterfaceMethodHash axis_sig{0xaa, 0xbb};

    const InterfaceMethod methods[] = {
        {    axis_sig,  interface_entry(&vec2_axis), 0, 0},
        {{0xcc, 0xdd}, interface_entry(&vec2_scale), 2, 0}, // slot 1 is unbound
    };
    InterfaceRegistry<4> registry;
    REQUIRE(registry.add({ivec, vec_id, methods, 2, 1, 0}) == InterfaceStatus::ok);

    Vec2 v{1, 2};
    InterfaceRef<4> ref;
    REQUIRE(ref.resolve(registry, &v, vec_id, ivec, 1) == InterfaceStatus::ok);
    REQUIRE(ref.signature(0) == axis_sig);
    // A matching signature, or no expectation at all, is ok.
    REQUIRE(ref.verify(0, axis_sig) == InterfaceStatus::ok);
    REQUIRE(ref.verify(0, InterfaceMethodHash{0x0, 0x0}) == InterfaceStatus::ok);
    // A different signature is refused, not silently called.
    REQUIRE(ref.verify(0, InterfaceMethodHash{0xde, 0xad}) == InterfaceStatus::signature_mismatch);
    // An unbound slot is reported as such.
    REQUIRE(ref.verify(1, axis_sig) == InterfaceStatus::method_not_found);

    AxisFn axis = nullptr;
    REQUIRE(ref.method(axis, 0, axis_sig) == InterfaceStatus::ok);
    REQUIRE(axis(ref.object(), 0) == 1);
    AxisFn unbound = reinterpret_cast<AxisFn>(&vec2_axis);
    REQUIRE(ref.method(unbound, 1, axis_sig) == InterfaceStatus::method_not_found);
    REQUIRE(unbound == nullptr);
}

namespace {
// A provider adapter whose first parameter is the object handle.
int counter_area(const void *self) { return static_cast<const Counter *>(self)->n; }
}   // namespace

// Manual registration has no macro: write the Traits and proxy by hand.
struct TestShapeTraits {
    static constexpr InterfaceId id{0x71, 0x00};
    static constexpr uint32_t version = 1;
    static constexpr skl::mics::util::array<InterfaceMethodHash, 1> signatures = {{{0x1111, 0x2222}}};
    struct alignas(8) Slots {
        uint32_t area;
    };
    alignas(8) static constexpr Slots slots = skl::mics::util::make_iota<Slots, signatures.size(), 0>();
};
class TestShape : public UniqueInterfaceProxy<TestShapeTraits> {
    using Base = UniqueInterfaceProxy<TestShapeTraits>;

public:
    using Base::Base;
    int area() const noexcept { return this->template call<int (*)(const void *)>(TestShapeTraits::slots.area); }
    InterfaceStatus area(int &out) const noexcept {
        return this->template call_checked<int (*)(const void *)>(out, TestShapeTraits::slots.area);
    }
};

// The proxy path keeps only the object handle, the highest bound slot and the
// adapter entries; provider signatures and identity metadata are not retained.
static_assert(
    sizeof(TestShape) < sizeof(InterfaceRef<1>), "UniqueInterfaceProxy must stay smaller than a full InterfaceRef");

TEST_CASE("an interface proxy binds, verifies and calls through cached entries") {
    constexpr TypeId counter_id{0x21, 0x22};
    const InterfaceMethod methods[] = {
        {{0x1111, 0x2222}, interface_entry(&counter_area), 0, 0}
    };
    InterfaceRegistry<4> registry;
    REQUIRE(registry.add({TestShapeTraits::id, counter_id, methods, 1, 1, 0}) == InterfaceStatus::ok);

    Counter c{7};
    TestShape shape(registry, &c, counter_id);
    REQUIRE(shape.valid());
    REQUIRE(shape.area() == 7);
    int out = 0;
    REQUIRE(shape.area(out) == InterfaceStatus::ok);
    REQUIRE(out == 7);

    // A bound method whose recorded signature differs from the contract's expected
    // signature makes the proxy invalid instead of calling it unsafely.
    const InterfaceMethod wrong[] = {
        {{0xdead, 0xbeef}, interface_entry(&counter_area), 0, 0}
    };
    InterfaceRegistry<4> registry2;
    REQUIRE(registry2.add({TestShapeTraits::id, counter_id, wrong, 1, 1, 0}) == InterfaceStatus::ok);
    TestShape bad(registry2, &c, counter_id);
    REQUIRE(bad.status() == InterfaceStatus::signature_mismatch);
    REQUIRE_FALSE(bad.valid());

    // Failure to bind is explicit and observable.
    TestShape missing;
    REQUIRE(missing.bind(registry, &c, TypeId{0xde, 0xad}) == InterfaceStatus::interface_not_found);
    REQUIRE_FALSE(missing.valid());
}

namespace {

constexpr auto member_i32 = skl::abix::model::canonical_type_id("prim:i32");

struct MemberBox {
    int32_t width;
    int32_t height;

    int32_t area() const noexcept { return width * height; }
    int32_t resize(int32_t w, int32_t h) noexcept {
        width = w;
        height = h;
        return width * height;
    }
};

constexpr TypeId member_box_id{0x31, 0x32};

}   // namespace

#define MEMBER_SHAPE_METHODS(X)      \
    X(area, int32_t, member_i32, {}) \
    X(resize, int32_t, member_i32, {member_i32, member_i32})

SKL_ABIX_INTERFACE_DECLARE_BOTH(MemberShape, MEMBER_SHAPE_METHODS, 1);

namespace {

#define MEMBER_SHAPE_ENTRY_LIST(X)               \
    X(MemberShapeTraits, area, &MemberBox::area) \
    X(MemberShapeTraits, resize, &MemberBox::resize)

SKL_ABIX_INTERFACE_MEMBER_TABLE(MemberShape, MEMBER_SHAPE_ENTRY_LIST);

}   // namespace

TEST_CASE("member table and implementation sugar build registry records") {
    InterfaceRegistry<4> registry;
    REQUIRE(registry.add(SKL_ABIX_INTERFACE_IMPLEMENTATION(MemberShape, member_box_id, 0)) == InterfaceStatus::ok);

    MemberBox box{3, 4};
    MemberShape shape(registry, &box, member_box_id);
    REQUIRE(shape.valid());
    REQUIRE(shape.area() == 12);
    REQUIRE(shape.resize(8, 6) == 48);
}

TEST_CASE("member functions can be registered directly through the member-entry macro") {
    const InterfaceMethod methods[] = {
        SKL_ABIX_INTERFACE_MEMBER_ENTRY(MemberShapeTraits, area, &MemberBox::area),
        SKL_ABIX_INTERFACE_MEMBER_ENTRY(MemberShapeTraits, resize, &MemberBox::resize),
    };

    InterfaceRegistry<4> registry;
    REQUIRE(registry.add({MemberShapeTraits::id, member_box_id, methods, 2, MemberShapeTraits::version, 0})
            == InterfaceStatus::ok);

    MemberBox box{3, 4};
    MemberShape shape(registry, &box, member_box_id);
    REQUIRE(shape.valid());
    REQUIRE(shape.area() == 12);

    const int32_t width = 8;
    const int32_t height = 6;
    REQUIRE(shape.resize(width, height) == 48);
    REQUIRE(box.width == width);
    REQUIRE(box.height == height);
}

TEST_CASE("shared slot tables drive many proxies with one entry table") {
    const InterfaceMethod methods[] = {
        SKL_ABIX_INTERFACE_MEMBER_ENTRY(MemberShapeTraits, area, &MemberBox::area),
        SKL_ABIX_INTERFACE_MEMBER_ENTRY(MemberShapeTraits, resize, &MemberBox::resize),
    };

    InterfaceRegistry<4> registry;
    REQUIRE(registry.add({MemberShapeTraits::id, member_box_id, methods, 2, MemberShapeTraits::version, 0})
            == InterfaceStatus::ok);

    skl::abix::poly::InterfaceSlotTable<MemberShapeTraits::signatures.size()> table;
    REQUIRE(skl::abix::poly::make_shared_slot_table<MemberShapeTraits>(table, registry, member_box_id)
            == InterfaceStatus::ok);
    REQUIRE(table.valid());
    REQUIRE(table.method_count() == 2);

    MemberBox first{3, 4};
    MemberBox second{5, 6};
    SharedMemberShape a(table, &first);
    SharedMemberShape b(table, &second);

    REQUIRE(a.valid());
    REQUIRE(b.valid());
    REQUIRE(a.area() == 12);
    REQUIRE(b.area() == 30);
    REQUIRE(a.verify(MemberShapeTraits::slots.area) == InterfaceStatus::ok);

    const int32_t width = 8;
    const int32_t height = 6;
    REQUIRE(a.resize(width, height) == 48);
    REQUIRE(first.width == width);
    REQUIRE(second.width == 5);   // b still dispatches to its own object

    static_assert(sizeof(SharedMemberShape) < sizeof(MemberShape), "shared proxy is the compact option");
}

#define STORAGE_SHAPE_METHODS(X) X(area, int32_t, member_i32, {})

SKL_ABIX_INTERFACE_DECLARE(UNIQUE, InlineStorageShape, STORAGE_SHAPE_METHODS, 1);
SKL_ABIX_INTERFACE_DECLARE(SHARED, AltStorage, STORAGE_SHAPE_METHODS, 1);
SKL_ABIX_INTERFACE_DECLARE_BOTH(BothStorage, STORAGE_SHAPE_METHODS, 1);

TEST_CASE("storage-first declaration macro selects the requested proxy layout") {
    const InterfaceMethod methods[] = {
        SKL_ABIX_INTERFACE_MEMBER_ENTRY(InlineStorageShapeTraits, area, &MemberBox::area),
    };

    InterfaceRegistry<8> registry;
    REQUIRE(
        registry.add({InlineStorageShapeTraits::id, member_box_id, methods, 1, InlineStorageShapeTraits::version, 0})
        == InterfaceStatus::ok);
    REQUIRE(registry.add({AltStorageTraits::id, member_box_id, methods, 1, AltStorageTraits::version, 0})
            == InterfaceStatus::ok);
    REQUIRE(registry.add({BothStorageTraits::id, member_box_id, methods, 1, BothStorageTraits::version, 0})
            == InterfaceStatus::ok);

    MemberBox box{3, 4};

    InlineStorageShape unique(registry, &box, member_box_id);
    REQUIRE(unique.valid());
    REQUIRE(unique.area() == 12);

    skl::abix::poly::InterfaceSlotTable<AltStorageTraits::signatures.size()> alt_table;
    REQUIRE(skl::abix::poly::make_shared_slot_table<AltStorageTraits>(alt_table, registry, member_box_id)
            == InterfaceStatus::ok);
    SharedAltStorage shared(alt_table, &box);
    REQUIRE(shared.valid());
    REQUIRE(shared.area() == 12);

    skl::abix::poly::InterfaceSlotTable<BothStorageTraits::signatures.size()> both_table;
    REQUIRE(skl::abix::poly::make_shared_slot_table<BothStorageTraits>(both_table, registry, member_box_id)
            == InterfaceStatus::ok);
    SharedBothStorage both_shared(both_table, &box);
    REQUIRE(both_shared.valid());
    REQUIRE(both_shared.area() == 12);

    static_assert(sizeof(InlineStorageShape) > sizeof(SharedAltStorage), "unique stores inline slot entries");
    static_assert(sizeof(BothStorage) > sizeof(SharedBothStorage), "both generates distinct proxy layouts");
}

#define STATIC_SHAPE_METHODS(X)       \
    X(value, int32_t, member_i32, {}) \
    X(plain, int32_t, member_i32, {})

SKL_ABIX_INTERFACE_DECLARE(UNIQUE, StaticShape, STATIC_SHAPE_METHODS, 1);

namespace {

struct StaticProvider {
    static int32_t value() noexcept { return 7; }
    static int32_t plain() { return 8; }
};

constexpr TypeId static_provider_id{0x41, 0x42};

}   // namespace

TEST_CASE("static member functions can be adapted to an interface entry") {
    const InterfaceMethod methods[] = {
        SKL_ABIX_INTERFACE_MEMBER_ENTRY(StaticShapeTraits, value, &StaticProvider::value),
        SKL_ABIX_INTERFACE_MEMBER_ENTRY(StaticShapeTraits, plain, &StaticProvider::plain),
    };

    InterfaceRegistry<4> registry;
    REQUIRE(registry.add({StaticShapeTraits::id, static_provider_id, methods, 2, StaticShapeTraits::version, 0})
            == InterfaceStatus::ok);

    int object = 0;   // static members ignore the handle, but binding still needs an object
    StaticShape shape(registry, &object, static_provider_id);
    REQUIRE(shape.valid());
    REQUIRE(shape.value() == 7);
    REQUIRE(shape.plain() == 8);
}

TEST_CASE("method_signature accepts pointer, StaticArray and variadic parameter lists") {
    constexpr auto i32 = skl::abix::model::canonical_type_id("prim:i32");
    constexpr auto v = skl::abix::model::canonical_type_id("prim:void");
    constexpr skl::abix::model::TypeId params[] = {i32, i32};

    constexpr auto by_pointer = skl::abix::poly::method_signature("resize", v, params, 2);
    constexpr auto by_array = skl::abix::poly::method_signature("resize", v, skl::mics::util::array{i32, i32});
    constexpr auto by_variadic = skl::abix::poly::method_signature("resize", v, i32, i32);

    STATIC_REQUIRE(by_pointer == by_array);
    STATIC_REQUIRE(by_pointer == by_variadic);
    STATIC_REQUIRE(
        skl::abix::poly::method_signature("clear", v) == skl::abix::poly::method_signature("clear", v, nullptr, 0));
}

TEST_CASE("version helper macros pack and extract a semantic version") {
    constexpr unsigned int version = SKL_ABIX_VERSION_NUMBER(1, 2, 3);
    STATIC_REQUIRE(version == 10'200'003U);
    uint32_t major, minor, patch;
    SKL_ABIX_VERSION_EXTRACT(version, major, minor, patch);
    REQUIRE(major == 1);
    REQUIRE(minor == 2);
    REQUIRE(patch == 3);

    constexpr unsigned int largest = SKL_ABIX_VERSION_NUMBER(99, 99, 99'999);
    uint32_t largest_major, largest_minor, largest_patch;
    SKL_ABIX_VERSION_EXTRACT(largest, largest_major, largest_minor, largest_patch);
    REQUIRE(largest_major == 99);
    REQUIRE(largest_minor == 99);
    REQUIRE(largest_patch == 99'999);
}
