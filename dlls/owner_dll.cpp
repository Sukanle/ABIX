/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0.
 */
#include "SKL/ABIX/ABIX.h"   // IWYU pragma: keep
#include "owner_plugin.h"

// A provider that combines the Interface and Owner ABIs: it advertises an
// interface through the fixed manifest entry, and creates objects whose destroy
// entry stays inside the module. A consumer calls the object through an
// interface binding and releases it through the creator's entry point, so the
// object's lifetime and its module's lifetime stay coupled.

namespace owner_plugin {

struct Counter {
    int value;
};

static int counter_add(const void *self, int delta) {
    auto *counter = const_cast<Counter *>(static_cast<const Counter *>(self));
    counter->value += delta;
    return counter->value;
}

static void counter_destroy(void *object) { delete static_cast<Counter *>(object); }

static const skl::abix::poly::InterfaceMethod g_methods[] = {
    {{0xa1, 0xb2}, skl::abix::poly::interface_entry(&counter_add), 0, 0},
};
static const skl::abix::poly::InterfaceManifestEntry g_interfaces[] = {
    {counter_interface, counter_interface_version, 0, g_methods, 1, 0},
};
static const skl::abix::poly::ModuleManifest g_manifest = {
    skl::abix::poly::abix_manifest_version,
    1,
    0,
    1,
    g_interfaces,
    0,
    0,
    nullptr,
};

}   // namespace owner_plugin

SKL_ABIX_DEFINE_MODULE_QUERY(&owner_plugin::g_manifest)

// Create an object and hand back an opaque owner handle. The object's layout
// never crosses the boundary, and only this module may destroy it.
extern "C" SKL_ABIX_DLL_EXPORT skl::abix::owner::handle owner_counter_create(int base) {
    auto *counter = new owner_plugin::Counter{base};
    return skl::abix::owner::make_owned(counter, &owner_plugin::counter_destroy, 0);
}

// A minimal ABIX table so the loader accepts the module. The owner create entry
// returns a POD handle and is resolved by its fixed symbol name; the table only
// carries a primitive-typed version probe.
extern "C" SKL_ABIX_DLL_EXPORT int owner_module_version(void) { return 1; }
SKL_ABIX_DEFINE_TABLE(SKL_ABIX_ENTRY("owner_module_version", owner_module_version))
