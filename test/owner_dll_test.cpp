/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0.
 */
#include "test_common.hpp"

#include "dlls/owner_plugin.h"

TEST_CASE("13.owner and interface across a DLL", "[interface][owner]") {
    skl::abix::dll::Object lib;
    if (!lib.load(dll_path("owner_dll").c_str())) {
        WARN("missing owner_dll, skip");
        return;
    }
    using namespace skl::abix::poly;

    ModuleRegistry<> registry;
    ModuleControl control;
    control.name = "owner_dll";
    const DiscoveryResult discovery = discover_module(lib, control, registry);
    REQUIRE(discovery.ok());

    const auto *binding = registry.find_first(owner_plugin::counter_interface, owner_plugin::counter_interface_version);
    REQUIRE(binding != nullptr);

    // Create an object through the provider's entry point. The returned POD
    // handle carries the creator's destroy entry; the consumer never sees the
    // Counter layout.
    using CreateFn = skl::abix::owner::handle (*)(int);
    auto create = reinterpret_cast<CreateFn>(lib.resolve("owner_counter_create"));
    REQUIRE(create != nullptr);
    const skl::abix::owner::handle raw = create(40);
    REQUIRE(skl::abix::owner::valid(raw));

    {
        // Adopt with the module lease: the object can only be released while the
        // module that must destroy it is still loaded.
        const skl::abix::owner::ref object = skl::abix::owner::ref::adopt(raw, &control);
        REQUIRE(object.valid());
        REQUIRE(object.leased());

        auto bound = bind_interface(*binding, object.object());
        REQUIRE(bound.valid());
        using AddFn = int (*)(const void *, int);
        const auto add = bound.ref.method<AddFn>(0);
        REQUIRE(add != nullptr);
        CHECK(add(bound.ref.object(), 2) == 42);

        // A live owner handle keeps the module from unloading.
        REQUIRE(registry.unregister_module(control.id) == ModuleStatus::busy);
    }

    // The handle went out of scope: destroy ran inside the DLL and the lease was
    // released, so the module can unload.
    REQUIRE(registry.unregister_module(control.id) == ModuleStatus::ok);
    REQUIRE(registry.find_first(owner_plugin::counter_interface) == nullptr);
    REQUIRE(lib.unload());
}
