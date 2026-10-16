/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0.
 */
#include "test_common.hpp"

#include "dlls/interface_plugin.h"

TEST_CASE("12.interface discovery and dispatch across a DLL", "[interface]") {
    skl::abix::dll::Object lib;
    if (!lib.load(dll_path("interface_dll").c_str())) {
        WARN("missing interface_dll, skip");
        return;
    }
    using namespace skl::abix::poly;

    ModuleRegistry<> registry;
    ModuleControl control;
    control.name = "interface_dll";

    // Discover through the fixed entry, validate the manifest and register.
    const DiscoveryResult discovery = discover_module(lib, control, registry);
    REQUIRE(discovery.manifest == ManifestStatus::ok);
    REQUIRE(discovery.registration == ModuleStatus::ok);
    REQUIRE(control.id != invalid_module_instance);
    REQUIRE(registry.module_count() == 1);

    const auto *binding =
        registry.find_first(interface_plugin::counter_interface, interface_plugin::counter_interface_version);
    REQUIRE(binding != nullptr);
    REQUIRE(binding->module == control.id);

    // The provider's object handle comes from its own exported symbol; the
    // interface table never exposes the counter layout.
    using InstanceFn = const void *(*)(void);
    auto instance = reinterpret_cast<InstanceFn>(lib.resolve("interface_instance"));
    REQUIRE(instance != nullptr);
    void *object = const_cast<void *>(instance());
    REQUIRE(object != nullptr);

    {
        auto bound = bind_interface(*binding, object);
        REQUIRE(bound.valid());
        using AddFn = int (*)(const void *, int);
        const auto add = bound.ref.method<AddFn>(0);
        REQUIRE(add != nullptr);
        CHECK(add(bound.ref.object(), 5) == 105);
    }

    // With the binding's lease released, the module unloads and its bindings
    // are retired: a stale cached pointer can never be called.
    REQUIRE(registry.unregister_module(control.id) == ModuleStatus::ok);
    REQUIRE(registry.find_first(interface_plugin::counter_interface) == nullptr);
    REQUIRE(lib.unload());
}

// A lease takes an OS-level reference to the module (Windows: GetModuleHandleEx
// on the base address; POSIX: dlopen by path), so force-unloading the Object
// must not unmap it while the lease is held: the adapter lives in the DLL and
// stays callable. Releasing the lease lets the module unload.
TEST_CASE("12b.interface lease keeps the module mapped across unload", "[interface]") {
    skl::abix::dll::Object lib;
    if (!lib.load(dll_path("interface_dll").c_str())) {
        WARN("missing interface_dll, skip");
        return;
    }
    using namespace skl::abix::poly;

    ModuleRegistry<> registry;
    ModuleControl control;
    control.name = "interface_dll";
    REQUIRE(discover_module(lib, control, registry).ok());
    CHECK(control.module_handle != nullptr);
    CHECK(control.pin != nullptr);

    const auto *binding =
        registry.find_first(interface_plugin::counter_interface, interface_plugin::counter_interface_version);
    REQUIRE(binding != nullptr);

    using InstanceFn = const void *(*)(void);
    auto instance = reinterpret_cast<InstanceFn>(lib.resolve("interface_instance"));
    REQUIRE(instance != nullptr);
    void *object = const_cast<void *>(instance());

    {
        auto bound = bind_interface(*binding, object);
        REQUIRE(bound.valid());
        using AddFn = int (*)(const void *, int);
        const auto add = bound.ref.method<AddFn>(0);
        REQUIRE(add != nullptr);
        CHECK(add(bound.ref.object(), 5) == 105);

        // The lease holds an OS reference: unmapping now would be a use-after-
        // free, but the pin keeps the DLL mapped so the adapter still works.
        lib.force_unload();
        CHECK(add(bound.ref.object(), 7) == 107);
    }

    REQUIRE(registry.unregister_module(control.id) == ModuleStatus::ok);
    lib.unload();
}
