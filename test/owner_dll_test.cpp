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

// M3.5/M3.6: the Interface/Owner lifecycle across an isolated CRT. The MinGW
// host loads an MSVC-compiled owner_dll, so the object is allocated and destroyed
// inside the DLL's CRT (only an opaque POD handle crosses), and the module lease
// pins the DLL (its CRT) while the object is alive.
TEST_CASE("13b.owner and interface across a cross-CRT MSVC DLL", "[interface][owner][msvc]") {
#ifdef SKL_ABIX_WINDOWS
    const std::string variant = "./variants/msvc_x64/owner_dll/owner_dll.dll";
    if (!file_exists(variant)) {
        WARN("missing MSVC owner_dll variant "
             << variant << " (run tools/build_variants.py --with-msvc first), skip cross-CRT owner test");
        return;
    }
    skl::abix::dll::Object lib;
    REQUIRE(lib.load(variant.c_str()));
    using namespace skl::abix::poly;

    ModuleRegistry<> registry;
    ModuleControl control;
    control.name = "owner_dll(msvc)";
    REQUIRE(discover_module(lib, control, registry).ok());

    const auto *binding =
        registry.find_first(owner_plugin::counter_interface, owner_plugin::counter_interface_version);
    REQUIRE(binding != nullptr);

    using CreateFn = skl::abix::owner::handle (*)(int);
    auto create = reinterpret_cast<CreateFn>(lib.resolve("owner_counter_create"));
    REQUIRE(create != nullptr);
    const skl::abix::owner::handle raw = create(40);
    REQUIRE(skl::abix::owner::valid(raw));

    {
        // Adopt with the module lease: destroy stays in the MSVC DLL's CRT.
        const skl::abix::owner::ref object = skl::abix::owner::ref::adopt(raw, &control);
        REQUIRE(object.valid());
        auto bound = bind_interface(*binding, object.object());
        REQUIRE(bound.valid());
        using AddFn = int (*)(const void *, int);
        const auto add = bound.ref.method<AddFn>(0);
        REQUIRE(add != nullptr);
        CHECK(add(bound.ref.object(), 2) == 42);
        // The live owner handle keeps the module (and its CRT) from unloading.
        REQUIRE(registry.unregister_module(control.id) == ModuleStatus::busy);
    }

    REQUIRE(registry.unregister_module(control.id) == ModuleStatus::ok);
    REQUIRE(lib.unload());
#else
    log_info("not Windows, skip the cross-CRT MSVC owner test");
#endif
}
