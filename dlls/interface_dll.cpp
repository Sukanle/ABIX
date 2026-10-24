/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0.
 */
#include "SKL/ABIX/ABIX.h"   // IWYU pragma: keep
#include "interface_plugin.h"

// A provider that implements one ABIX Interface and discovers it through the
// fixed entry `abix_module_query_v1`. It keeps its object private: the adapter
// receives the opaque handle and the consumer never inspects the layout.

namespace interface_plugin {

struct Counter {
    int base;
};

static Counter g_counter{100};

static int counter_add(const void *self, int value) { return static_cast<const Counter *>(self)->base + value; }

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

}   // namespace interface_plugin

SKL_ABIX_DEFINE_MODULE_QUERY(&interface_plugin::g_manifest)

// The provider's object handle. A consumer obtains it and passes it back to the
// interface adapter; it never interprets it.
extern "C" SKL_ABIX_DLL_EXPORT const void *interface_instance(void) { return &interface_plugin::g_counter; }

// A minimal ABIX table so the loader accepts the module; the interface itself is
// discovered through the fixed manifest entry, not the table.
SKL_ABIX_DEFINE_TABLE(SKL_ABIX_ENTRY("interface_instance", interface_instance))
