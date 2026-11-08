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
#ifndef SKL_ABIX_STD_OWNER_H
#define SKL_ABIX_STD_OWNER_H

#include "SKL/ABIX/Interface/Owner.h"

// ABIX-STD owner projection.
//
// This is deliberately not a new ABI type: Owner<T> is a source-level alias for
// the existing owner::handle. The template parameter is a source-level label
// for generated bindings; it does not add a field or change the layout.
// Concrete instances are named abix_owner_<Type> by the generator.
//
// Cross-language leases are provided by Interface/LeaseABI.h: the owner handle
// carries identity and destroy, while the lease keeps the module mapped until
// the object is released.

namespace abix_std {

template<class T>
using Owner = ::skl::abix::owner::handle;

}   // namespace abix_std

static_assert(sizeof(abix_std::Owner<int>) == sizeof(::skl::abix::owner::handle),
    "ABIX-STD Owner<T> must not change the owner::handle layout");
static_assert(alignof(abix_std::Owner<int>) == alignof(::skl::abix::owner::handle),
    "ABIX-STD Owner<T> must not change the owner::handle alignment");

#endif   // SKL_ABIX_STD_OWNER_H
