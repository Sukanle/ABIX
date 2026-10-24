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

#include <type_traits>

#include "ABIX/Model/ABI.h"
#include "mics/utils/array.h"

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

// Canonical interface-identity domain, matching `AMC::interface_identity`. The
// identity is the content hash of the canonical name, version and ordered
// method signatures, so two interfaces that share a name but differ in version
// or shape stay distinct.
//
// The domain value is the 8-byte ASCII tag "INTERFAC" packed big-endian:
//   0x494E544552464143 = 'I' 'N' 'T' 'E' 'R' 'F' 'A' 'C'
// It is a deliberate truncation of "INTERFACE", which does not fit a uint64_t.
//
// It is never serialized as bytes: `hash_text` mixes it into the initial state
// as an integer (`lo ^= domain`, `hi += domain`), so there is no endianness
// choice to make. A cross-language implementation must use the same integer and
// the same mixing, not a byte-array prefix.
constexpr uint64_t interface_identity_domain = 0X494E544552464143ULL;

// A method's signature paired with its dispatch slot. Used to compute an
// interface identity from a method list that is not yet in slot order.
struct InterfaceMethodKey {
    InterfaceMethodHash signature;
    uint32_t slot;
};

// Hash one unsigned value as the decimal text AMC's `std::to_string` would
// produce, continuing a canonical hash state. `interface_identity` builds its
// input by concatenating decimal fields, so this must emit the same digits.
constexpr util::CanonicalState interface_hash_u64(util::CanonicalState state, uint64_t value) noexcept {
    char digits[20] = {};
    uint32_t count = 0;
    if (value == 0) digits[count++] = '0';
    while (value != 0) {
        digits[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    }
    for (uint32_t i = count; i-- > 0;)
        state = util::canonical_hash_byte(state, static_cast<uint8_t>(digits[i]));
    return state;
}

// The interface identity for a canonical name, a version and method signatures
// already ordered by slot. This is exactly the byte string and algorithm
// `AMC::interface_identity` hashes, so a hand-written provider and AMC derive
// the same id. `name` must be the same canonical spelling AMC uses.
constexpr InterfaceId interface_id(
    const char *name, uint32_t version, const InterfaceMethodHash *signatures, size_t count) noexcept {
    util::CanonicalState state = util::canonical_hash_begin(interface_identity_domain);
    state = util::canonical_hash_bytes(state, "ABIX-INTERFACE:", sizeof("ABIX-INTERFACE:") - 1);
    for (size_t i = 0; name && name[i] != '\0'; ++i)
        state = util::canonical_hash_byte(state, static_cast<uint8_t>(name[i]));
    state = util::canonical_hash_byte(state, static_cast<uint8_t>(':'));
    state = interface_hash_u64(state, version);
    for (size_t i = 0; i < count; ++i) {
        state = util::canonical_hash_byte(state, static_cast<uint8_t>(':'));
        state = interface_hash_u64(state, signatures[i].lo);
        state = util::canonical_hash_byte(state, static_cast<uint8_t>(':'));
        state = interface_hash_u64(state, signatures[i].hi);
    }
    return InterfaceId{state.lo, state.hi};
}

// The interface identity for a canonical name, a version and method signatures
// already ordered by slot. This is exactly the byte string and algorithm
// `AMC::interface_identity` hashes, so a hand-written provider and AMC derive
// the same id. `name` must be the same canonical spelling AMC uses.
//   interface_id("Shape", 1, mics::utils::array{sig_a, sig_b})
template<size_t Count>
constexpr InterfaceId interface_id(
    const char *name, uint32_t version, const mics::utils::array<InterfaceMethodHash, Count> &signatures) noexcept {
    return interface_id(name, version, signatures.data(), Count);
}

// Variadic signature list, zero allocation:
//   interface_id("Shape", 1, sig_a, sig_b)
template<typename... Signatures,
    typename = std::enable_if_t<(std::is_same<Signatures, InterfaceMethodHash>::value && ...)>>
constexpr InterfaceId interface_id(const char *name, uint32_t version, Signatures... signatures) noexcept {
    if constexpr (sizeof...(Signatures) == 0) {
        return interface_id(name, version, mics::utils::array<InterfaceMethodHash, 0>{});
    } else {
        const mics::utils::array<InterfaceMethodHash, sizeof...(Signatures)> list = {signatures...};
        return interface_id(name, version, list);
    }
}

// Overload for a method list that is not yet ordered: the entries are sorted by
// slot before hashing, matching `AMC::interface_hash`. `ABIX_DECLARE_INTERFACE`
// uses this so the method list can be written in any order.
//   interface_id("Shape", 1, mics::utils::array{InterfaceMethodKey{...}, ...})
template<size_t Count>
constexpr InterfaceId interface_id(
    const char *name, uint32_t version, const mics::utils::array<InterfaceMethodKey, Count> &methods) noexcept {
    InterfaceMethodKey sorted[Count == 0 ? 1 : Count] = {};
    mics::utils::array<InterfaceMethodHash, Count> signatures = {};
    for (size_t i = 0; i < Count; ++i)
        sorted[i] = methods[i];
    for (size_t i = 1; i < Count; ++i) {
        InterfaceMethodKey key = sorted[i];
        size_t j = i;
        while (j > 0 && sorted[j - 1].slot > key.slot) {
            sorted[j] = sorted[j - 1];
            --j;
        }
        sorted[j] = key;
    }
    for (size_t i = 0; i < Count; ++i)
        signatures[i] = sorted[i].signature;
    return interface_id(name, version, signatures);
}

// AMC's method signature hash, matching `AMC::signature_hash` for the
// [[interface]] path. The canonical input is concatenated without separators:
//   name + decimal(return.lo) + decimal(return.hi)
//   + for each parameter: decimal(type.lo) + decimal(type.hi)
// Parameter names are deliberately absent: AMC does not hash them. `name` must
// be the canonical spelling AMC records and the TypeIDs come from
// `model::canonical_type_id` or an AMC artifact. This removes the 64-bit
// constants from a hand-written contract, but it still needs the same inputs
// AMC used: it cannot infer a C++ spelling such as `int32_t` (AMC keys that as
// `prim:i32`) from the preprocessor.
constexpr InterfaceMethodHash method_signature(
    const char *name, model::TypeId return_type, const model::TypeId *parameters, size_t count) noexcept {
    util::CanonicalState state = util::canonical_hash_begin(0X5349474E41545552ULL);
    for (size_t i = 0; name != nullptr && name[i] != '\0'; ++i)
        state = util::canonical_hash_byte(state, static_cast<uint8_t>(name[i]));
    state = interface_hash_u64(state, return_type.lo);
    state = interface_hash_u64(state, return_type.hi);
    for (size_t i = 0; i < count; ++i) {
        state = interface_hash_u64(state, parameters[i].lo);
        state = interface_hash_u64(state, parameters[i].hi);
    }
    return InterfaceMethodHash{state.lo, state.hi};
}

// Inline parameter list, zero allocation:
//   method_signature("resize", void_id, mics::utils::array{i32, i32})
template<size_t Count>
constexpr InterfaceMethodHash method_signature(
    const char *name, model::TypeId return_type, const mics::utils::array<model::TypeId, Count> &parameters) noexcept {
    return method_signature(name, return_type, parameters.data(), Count);
}

constexpr InterfaceMethodHash method_signature(const char *name, model::TypeId return_type,
    const mics::utils::array<mics::utils::empty_element, 0> &parameters) noexcept {
    (void)parameters;
    return method_signature(name, return_type, nullptr, 0);
}

// Same inline form with an explicit count (the declaration macro passes one).
// The array's own size is authoritative; `count` exists only to keep the macro
// entry shape uniform.
template<size_t Count>
constexpr InterfaceMethodHash method_signature(const char *name, model::TypeId return_type,
    const mics::utils::array<model::TypeId, Count> &parameters, size_t) noexcept {
    return method_signature(name, return_type, parameters.data(), Count);
}

// Variadic parameter list, zero allocation:
//   method_signature("resize", void_id, i32, i32)
// The `enable_if` keeps this overload out of the pointer + count calls and the
// `mics::utils::array` form; a non-TypeId argument is a compile error, not a silent
// conversion.
template<typename... Params, typename = std::enable_if_t<(std::is_same<Params, model::TypeId>::value && ...)>>
constexpr InterfaceMethodHash method_signature(
    const char *name, model::TypeId return_type, Params... parameters) noexcept {
    if constexpr (sizeof...(Params) == 0) {
        return method_signature(name, return_type, nullptr, 0);
    } else {
        const model::TypeId list[] = {parameters...};
        return method_signature(name, return_type, list, sizeof...(Params));
    }
}

enum class InterfaceStatus : uint32_t {
    ok = 0,
    invalid_table = 1,
    duplicate_implementation = 2,
    capacity_exceeded = 3,
    // No implementation for (concrete_type, interface_id) at all.
    interface_not_found = 4,
    // An implementation exists, but not for the requested version.
    version_mismatch = 5,
    // The requested slot is not bound in this implementation.
    method_not_found = 6,
    // The method table has a slot above the handle's capacity.
    method_capacity_exceeded = 7,
    // The bound method's recorded signature differs from the expected one, so a
    // call through the assumed function type would be unsafe.
    signature_mismatch = 8,
};

static_assert(
    static_cast<uint32_t>(InterfaceStatus::ok) == 0, "InterfaceStatus values are a diagnostics compatibility surface");
static_assert(static_cast<uint32_t>(InterfaceStatus::signature_mismatch) == 8,
    "InterfaceStatus values are a diagnostics compatibility surface");

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
