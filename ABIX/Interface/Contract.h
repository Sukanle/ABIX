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
#ifndef SKL_ABIX_INTERFACE_CONTRACT_H
#define SKL_ABIX_INTERFACE_CONTRACT_H

#include <stdint.h>
#include <string.h>

#include "ABIX/Model/ABI.h"

// ABIX Interface layer — the cross-language interface contract.
//
// An interface is an independent contract, not a class hierarchy and not any
// compiler's native vtable. A provider keeps its own object layout, inheritance
// and virtual functions; a generated adapter (which receives the provider's
// object handle as its first argument) realises each interface method.
//
//   * Interface identity is the content hash AMC computes over the interface
//     name, version and ordered method signatures.
//   * Method identity is (interface_id, slot).
//   * A binding names the adapter by signature, never a vtable slot.
//
// Neither identity is derived from a class name or a vtable address, so two
// languages agree without sharing an object model. These are contracts, not
// layout facts, so nothing here participates in the type/layout/ABI identity.

namespace skl::abix::poly {

// Stable interface identity: the AMC-computed content hash. Never a class name
// or a vtable address.
using InterfaceId = Hash128;

// One method's ABI signature, derived the same way as a function signature.
using InterfaceMethodHash = model::SignatureHash;

// One interface method's adapter. `signature` is the method ABI signature hash
// recorded by AMC; `entry` is the adapter entry point, called with the object
// handle as its first argument. A zero `entry` means the slot is unbound.
struct InterfaceMethod {
    InterfaceMethodHash signature;
    uintptr_t entry;
    uint32_t slot;
    uint32_t flags;
};

// A concrete type's implementation of one interface. The method table is
// supplied by the provider (generated from the native adapters); the concrete
// type itself is not modified. `version` is the interface version.
struct InterfaceImplementation {
    InterfaceId interface_id;
    model::TypeId concrete_type;
    const InterfaceMethod *methods;
    uint32_t method_count;
    uint32_t version;
    uint32_t flags;
};

enum class InterfaceStatus : uint32_t {
    ok = 0,
    invalid_table,
    duplicate_implementation,
    capacity_exceeded,
    // No implementation for (concrete_type, interface_id) at all.
    interface_not_found,
    // An implementation exists, but not for the requested version.
    version_mismatch,
    // The requested slot is not bound in this implementation.
    method_not_found,
    // The method table has a slot above the handle's capacity.
    method_capacity_exceeded,
    // The bound method's recorded signature differs from the expected one, so a
    // call through the assumed function type would be unsafe.
    signature_mismatch,
};

constexpr const char *to_string(InterfaceStatus status) noexcept {
    switch (status) {
        case InterfaceStatus::ok:                       return "ok";
        case InterfaceStatus::invalid_table:            return "invalid_table";
        case InterfaceStatus::duplicate_implementation: return "duplicate_implementation";
        case InterfaceStatus::capacity_exceeded:        return "capacity_exceeded";
        case InterfaceStatus::interface_not_found:      return "interface_not_found";
        case InterfaceStatus::version_mismatch:         return "version_mismatch";
        case InterfaceStatus::method_not_found:         return "method_not_found";
        case InterfaceStatus::method_capacity_exceeded: return "method_capacity_exceeded";
        case InterfaceStatus::signature_mismatch:       return "signature_mismatch";
    }
    return "unknown";
}

// Copy a function pointer into the opaque `entry` slot. Kept here so generated
// registrations and callers spell it the same way.
template<typename Fn>
inline uintptr_t interface_entry(Fn fn) noexcept {
    static_assert(sizeof(Fn) <= sizeof(uintptr_t), "function pointer wider than uintptr_t");
    uintptr_t entry = 0;
    memcpy(&entry, &fn, sizeof(Fn));
    return entry;
}

}   // namespace skl::abix::poly

#endif   // SKL_ABIX_INTERFACE_CONTRACT_H
