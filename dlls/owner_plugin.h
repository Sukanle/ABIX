/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef SKL_ABIX_DLLS_OWNER_PLUGIN_H
#define SKL_ABIX_DLLS_OWNER_PLUGIN_H

#include "ABIX/Interface/Interface.h"

// Shared between the owner/interface plugin and its consumer test.
namespace owner_plugin {

using skl::abix::poly::InterfaceId;

inline constexpr InterfaceId counter_interface{0x2a3b4c5dULL, 0x6e7f8091ULL};
inline constexpr uint32_t counter_interface_version = 1;

// The provider's create entry: it allocates the object and returns an owner
// handle whose destroy entry lives inside the module, so release returns to the
// creating allocation domain.
using CreateFn = skl::abix::owner::handle (*)(int base);

}   // namespace owner_plugin

#endif   // SKL_ABIX_DLLS_OWNER_PLUGIN_H
