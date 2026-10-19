/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0.
 */
#include <catch2/catch_test_macros.hpp>

#include "ABIX/Interface/Declaration.h"
#include "ABIX/Interface/Interface.h"

namespace {

using skl::abix::model::TypeId;
using skl::abix::poly::interface_entry;
using skl::abix::poly::InterfaceId;
using skl::abix::poly::InterfaceMethod;
using skl::abix::poly::InterfaceMethodHash;
using skl::abix::poly::InterfaceRef;
using skl::abix::poly::InterfaceRegistry;
using skl::abix::poly::InterfaceStatus;

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

#define ABIX_TEST_SHAPE_METHODS(X) X(area, 0x1111, 0x2222, int, 0)
ABIX_DECLARE_INTERFACE(TestShape, 1, ABIX_TEST_SHAPE_METHODS)

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
