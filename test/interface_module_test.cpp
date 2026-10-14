/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0.
 */
#include <catch2/catch_test_macros.hpp>

#include "ABIX/Interface/Interface.h"

namespace {

using skl::abix::poly::abix_manifest_version;
using skl::abix::poly::acquire_lease;
using skl::abix::poly::bind_interface;
using skl::abix::poly::interface_entry;
using skl::abix::poly::InterfaceId;
using skl::abix::poly::InterfaceManifestEntry;
using skl::abix::poly::InterfaceMethod;
using skl::abix::poly::ManifestStatus;
using skl::abix::poly::module_state;
using skl::abix::poly::ModuleControl;
using skl::abix::poly::ModuleManifest;
using skl::abix::poly::ModuleRegistry;
using skl::abix::poly::ModuleState;
using skl::abix::poly::ModuleStatus;
using skl::abix::poly::validate_manifest;

// A provider object and its adapter. No vtable, never modified.
struct Obj {
    int base;
};
int obj_add(const void *self, int value) { return static_cast<const Obj *>(self)->base + value; }
using AddFn = int (*)(const void *, int);

struct Adder {
    Obj object{10};
    InterfaceMethod methods[1];
    ModuleManifest manifest;

    explicit Adder(uint32_t interface_version = 1, InterfaceId id = InterfaceId{0x11, 0x12}) {
        methods[0] = {
            {0xa1, 0x0},
            interface_entry(&obj_add), 0, 0
        };
        manifest.manifest_version = abix_manifest_version;
        manifest.module_abi_version = 1;
        manifest.flags = 0;
        manifest.interface_count = 1;
        manifest.interfaces = entries;
        manifest.extension_count = 0;
        manifest.reserved = 0;
        manifest.extensions = nullptr;
        entries[0] = {id, interface_version, 0, methods, 1, 0};
    }

private:
    InterfaceManifestEntry entries[1]{};
};

}   // namespace

TEST_CASE("module manifest validation rejects malformed shapes") {
    Adder adder;
    REQUIRE(validate_manifest(&adder.manifest) == ManifestStatus::ok);
    REQUIRE(validate_manifest(nullptr) == ManifestStatus::null_manifest);

    ModuleManifest wrong_version = adder.manifest;
    wrong_version.manifest_version = abix_manifest_version + 1;
    REQUIRE(validate_manifest(&wrong_version) == ManifestStatus::unsupported_version);

    ModuleManifest null_interfaces = adder.manifest;
    null_interfaces.interfaces = nullptr;
    REQUIRE(validate_manifest(&null_interfaces) == ManifestStatus::invalid_interface);

    InterfaceManifestEntry bad_entry = adder.manifest.interfaces[0];
    bad_entry.interface_id = InterfaceId{};
    ModuleManifest zero_id = adder.manifest;
    zero_id.interfaces = &bad_entry;
    REQUIRE(validate_manifest(&zero_id) == ManifestStatus::invalid_interface);

    bad_entry = adder.manifest.interfaces[0];
    bad_entry.methods = nullptr;
    ModuleManifest null_methods = adder.manifest;
    null_methods.interfaces = &bad_entry;
    REQUIRE(validate_manifest(&null_methods) == ManifestStatus::invalid_interface);
}

TEST_CASE("a discovered module registers, dispatches and unloads") {
    ModuleRegistry<> registry;
    Adder adder;
    ModuleControl control;
    control.name = "adder";

    REQUIRE(registry.register_module(control, adder.manifest) == ModuleStatus::ok);
    REQUIRE(registry.module_count() == 1);
    REQUIRE(registry.interface_count() == 1);
    REQUIRE(control.id != 0);
    REQUIRE(module_state(control) == ModuleState::active);

    const auto *binding = registry.find_first(InterfaceId{0x11, 0x12}, 1);
    REQUIRE(binding != nullptr);
    REQUIRE(binding->module == control.id);
    REQUIRE(binding->method_count == 1);

    // Bind once with a lease, then call natively. The lease is scoped so the
    // module can be unloaded afterwards.
    {
        auto bound = bind_interface(*binding, &adder.object);
        REQUIRE(bound.valid());
        const auto add = bound.ref.method<AddFn>(0);
        REQUIRE(add != nullptr);
        CHECK(add(bound.ref.object(), 5) == 15);
    }

    // Unload with no leases: bindings are gone and the instance is retired.
    REQUIRE(registry.unregister_module(control.id) == ModuleStatus::ok);
    REQUIRE(registry.find_first(InterfaceId{0x11, 0x12}) == nullptr);
    REQUIRE_FALSE(registry.registered(control.id));
    REQUIRE(module_state(control) == ModuleState::unloaded);
    REQUIRE(registry.unregister_module(control.id) == ModuleStatus::not_registered);
}

TEST_CASE("several modules may implement the same interface") {
    ModuleRegistry<> registry;
    Adder first;
    Adder second;
    ModuleControl a;
    ModuleControl b;
    a.name = "a";
    b.name = "b";

    REQUIRE(registry.register_module(a, first.manifest) == ModuleStatus::ok);
    REQUIRE(registry.register_module(b, second.manifest) == ModuleStatus::ok);
    REQUIRE(registry.module_count() == 2);
    REQUIRE(registry.interface_count() == 2);

    const skl::abix::poly::ModuleInterface *found[4];
    REQUIRE(registry.find_interface(InterfaceId{0x11, 0x12}, 1, found, 4) == 2);
    CHECK(found[0]->module != found[1]->module);
}

TEST_CASE("re-registration is idempotent and duplicates are rejected") {
    ModuleRegistry<> registry;
    Adder adder;
    ModuleControl control;
    control.name = "idem";
    REQUIRE(registry.register_module(control, adder.manifest) == ModuleStatus::ok);
    REQUIRE(registry.register_module(control, adder.manifest) == ModuleStatus::already_registered);
    REQUIRE(registry.module_count() == 1);
    REQUIRE(registry.interface_count() == 1);

    // A manifest advertising the same (id, version) twice is rejected.
    Adder duplicated;
    InterfaceManifestEntry two[2] = {duplicated.manifest.interfaces[0], duplicated.manifest.interfaces[0]};
    ModuleManifest double_manifest = duplicated.manifest;
    double_manifest.interface_count = 2;
    double_manifest.interfaces = two;
    ModuleControl other;
    other.name = "dup";
    REQUIRE(registry.register_module(other, double_manifest) == ModuleStatus::duplicate_interface);
}

TEST_CASE("interface versions coexist and resolve by version") {
    ModuleRegistry<> registry;
    Adder v1(1);
    Adder v2(2);
    ModuleControl a;
    ModuleControl b;
    a.name = "v1";
    b.name = "v2";
    REQUIRE(registry.register_module(a, v1.manifest) == ModuleStatus::ok);
    REQUIRE(registry.register_module(b, v2.manifest) == ModuleStatus::ok);

    REQUIRE(registry.find_first(InterfaceId{0x11, 0x12}, 1)->version == 1);
    REQUIRE(registry.find_first(InterfaceId{0x11, 0x12}, 2)->version == 2);
    // Version 0 selects the newest registered revision.
    REQUIRE(registry.find_first(InterfaceId{0x11, 0x12}, 0)->version == 2);
    REQUIRE(registry.find_first(InterfaceId{0x11, 0x12}, 3) == nullptr);
}

TEST_CASE("unload is refused while a lease is held, then completes") {
    ModuleRegistry<> registry;
    Adder adder;
    ModuleControl control;
    control.name = "leased";
    REQUIRE(registry.register_module(control, adder.manifest) == ModuleStatus::ok);
    const auto *binding = registry.find_first(InterfaceId{0x11, 0x12}, 1);
    REQUIRE(binding != nullptr);

    auto bound = bind_interface(*binding, &adder.object);
    REQUIRE(bound.valid());
    // The module may not unload while the binding holds a lease.
    REQUIRE(registry.unregister_module(control.id) == ModuleStatus::busy);
    REQUIRE(module_state(control) == ModuleState::draining);
    // Draining refuses new leases, so a fresh binding cannot be made.
    auto refused = bind_interface(*binding, &adder.object);
    REQUIRE_FALSE(refused.valid());

    bound.lease.release();
    REQUIRE(registry.unregister_module(control.id) == ModuleStatus::ok);
    REQUIRE(registry.find_first(InterfaceId{0x11, 0x12}) == nullptr);
}

TEST_CASE("reloading the same module gets a fresh instance id") {
    ModuleRegistry<> registry;
    Adder adder;
    ModuleControl first;
    first.name = "reload";
    REQUIRE(registry.register_module(first, adder.manifest) == ModuleStatus::ok);
    const auto first_id = first.id;
    REQUIRE(registry.unregister_module(first_id) == ModuleStatus::ok);

    ModuleControl second;
    second.name = "reload";
    REQUIRE(registry.register_module(second, adder.manifest) == ModuleStatus::ok);
    // Same file, new load: never reuse the old identity, so a stale binding
    // can never be mistaken for a live one.
    REQUIRE(second.id != first_id);
    REQUIRE_FALSE(registry.registered(first_id));
}
