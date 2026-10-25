/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef SKL_ABIX_DLLS_INTERFACE_PLUGIN_H
#define SKL_ABIX_DLLS_INTERFACE_PLUGIN_H

#include "SKL/ABIX/Interface/Interface.h"

// Shared between the interface plugin and its consumer test: the interface
// identity a provider advertises and a consumer queries. It is a content hash
// in real use (AMC computes it); a fixed constant keeps the fixture small.
namespace interface_plugin {

using skl::abix::poly::InterfaceId;

inline constexpr InterfaceId counter_interface{0x1f2e3d4cULL, 0x5a6b7c8dULL};
inline constexpr uint32_t counter_interface_version = 1;

}   // namespace interface_plugin

#endif   // SKL_ABIX_DLLS_INTERFACE_PLUGIN_H
