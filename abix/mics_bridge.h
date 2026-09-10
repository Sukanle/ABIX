/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef SKL_ABIX_MICS_BRIDGE_H
#define SKL_ABIX_MICS_BRIDGE_H

#include "abi_model.h"
#include "runtime_descriptor.h"
#include "mics/rt/config.h"

namespace skl::abix::mics_bridge {

// This conversion is intentionally explicit even though both values have the
// same representation. It prevents accidental truncation at the API boundary.
constexpr mics::rt::TypeId to_mics_type_id(model::TypeId id) noexcept {
    return {id.lo, id.hi};
}

constexpr model::TypeId to_abix_type_id(mics::rt::TypeId id) noexcept {
    return {id.lo, id.hi};
}

static_assert(sizeof(model::TypeId) == sizeof(mics::rt::TypeId),
              "ABIX and MICS TypeId representations must remain compatible");

}  // namespace skl::abix::mics_bridge

#endif  // SKL_ABIX_MICS_BRIDGE_H
