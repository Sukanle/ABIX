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
 *
 * ABIX Interface layer: non-intrusive polymorphism. An interface is an
 * independent cross-language contract; providers are never modified and no
 * language's private vtable is exposed. Binding happens at setup time; the hot
 * path is a plain native indirect call.
 *
 * See docs/abix/interface.md for the design.
 */
#ifndef SKL_ABIX_INTERFACE_H
#define SKL_ABIX_INTERFACE_H

#include "ABIX/Interface/Contract.h"         // IWYU pragma: keep
#include "ABIX/Interface/Discovery.h"        // IWYU pragma: keep
#include "ABIX/Interface/Lease.h"            // IWYU pragma: keep
#include "ABIX/Interface/Module.h"           // IWYU pragma: keep
#include "ABIX/Interface/ModuleRegistry.h"   // IWYU pragma: keep
#include "ABIX/Interface/Owner.h"            // IWYU pragma: keep
#include "ABIX/Interface/Ref.h"              // IWYU pragma: keep
#include "ABIX/Interface/Registry.h"         // IWYU pragma: keep

#endif   // SKL_ABIX_INTERFACE_H
