/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef SKL_ABIX_BRIDGE_MICS_H
#define SKL_ABIX_BRIDGE_MICS_H

#include "ABIX/Model/ABI.h"
#include "mics/rt/config.h"

namespace skl::abix::bridge {

// This conversion is intentionally explicit even though both values have the
// same representation. It prevents accidental truncation at the API boundary.
constexpr skl::mics::rt::TypeId to_mics_type_id(model::TypeId id) noexcept { return {id.lo, id.hi}; }

constexpr model::TypeId to_abix_type_id(skl::mics::rt::TypeId id) noexcept { return {id.lo, id.hi}; }

static_assert(sizeof(model::TypeId) == sizeof(skl::mics::rt::TypeId),
    "ABIX and MICS TypeId representations must remain compatible");

}   // namespace skl::abix::bridge

#endif   // SKL_ABIX_MICS_BRIDGE_H
