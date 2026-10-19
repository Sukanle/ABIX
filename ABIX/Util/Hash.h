/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */
#ifndef SKL_ABIX_UTIL_HASH_H
#define SKL_ABIX_UTIL_HASH_H

#include <stddef.h>
#include <stdint.h>

namespace skl::abix {

// Hash values are data-model values. The selected algorithm is recorded by
// the containing artifact rather than being implied by this representation.
struct Hash128 {
    uint64_t lo;
    uint64_t hi;
};

constexpr bool operator==(Hash128 lhs, Hash128 rhs) noexcept { return lhs.lo == rhs.lo && lhs.hi == rhs.hi; }
constexpr bool operator!=(Hash128 lhs, Hash128 rhs) noexcept { return !(lhs == rhs); }

static_assert(sizeof(Hash128) == 16, "Hash128 must have a fixed wire size");

using Hash64 = uint64_t;

namespace util {
// FNV-1a 64 constants. `fnv1a_offset_basis` is also AMC's canonical `lo` seed
// (see AMC/src/Core/Core.cpp), so `canonical_hash_begin` shares it.
constexpr uint64_t fnv1a_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr uint64_t fnv1a_prime = 1'099'511'628'211ULL;
// Second-lane seed for the dual-lane reference format; not a standard FNV
// constant (it only exists to diverge the two lanes at the start).
constexpr uint64_t fnv1a_second_offset = 10'951'620'151'779'511'277ULL;
// AMC's `hi` lane multiplier, XXH64 PRIME64_2.
constexpr uint64_t canonical_hi_prime = 14'029'467'366'897'019'727ULL;

enum class Algorithm : uint16_t {
    fnv1a = 1,
    xxh3 = 2,
    blake3 = 3,
    sha256 = 4,
    fnv1a_dual_lane_reference = 0X10,
};

enum class Domain : uint16_t {
    type_identity = 1,
    layout_identity = 2,
    signature_identity = 3,
    artifact_identity = 4,
    map_identity = 5,
};

struct Descriptor {
    Algorithm algorithm;
    uint16_t algorithm_version;
    uint16_t digest_bits;
    Domain domain;
};

struct Value {
    Descriptor descriptor;
    Hash128 digest;
};

constexpr bool same_descriptor(const Descriptor &lhs, const Descriptor &rhs) noexcept {
    return lhs.algorithm == rhs.algorithm
        && lhs.algorithm_version == rhs.algorithm_version
        && lhs.digest_bits == rhs.digest_bits
        && lhs.domain == rhs.domain;
}

constexpr bool same_value(const Value &lhs, const Value &rhs) noexcept {
    return same_descriptor(lhs.descriptor, rhs.descriptor)
        && lhs.digest.lo == rhs.digest.lo
        && lhs.digest.hi == rhs.digest.hi;
}

constexpr bool valid(const Descriptor &descriptor) noexcept {
    return descriptor.algorithm_version != 0 && descriptor.digest_bits != 0 && descriptor.digest_bits <= 128;
}

// The dual-lane value is an explicitly named transitional reference format.
// It is not the standard FNV-1a-128 algorithm and is not a security hash.
constexpr uint64_t fnv1a64(const uint8_t *data, size_t size, uint64_t offset) noexcept {
    uint64_t result = offset;
    for (size_t i = 0; i < size; ++i) {
        result ^= data[i];
        result *= 1'099'511'628'211ULL;
    }
    return result;
}

constexpr Value fnv1a(const uint8_t *data, size_t size, Domain domain, uint16_t bits = 128) noexcept {
    const uint8_t domain_byte = static_cast<uint8_t>(domain);
    const uint64_t lo = fnv1a64(data, size, fnv1a_offset_basis ^ domain_byte);
    const uint64_t hi = fnv1a64(data, size, fnv1a_second_offset ^ (domain_byte << 8));
    const Algorithm algorithm = bits == 64 ? Algorithm::fnv1a : Algorithm::fnv1a_dual_lane_reference;
    return {
        {algorithm, 1, bits, domain},
        {lo, hi}
    };
}

constexpr uint64_t runtime_key(Hash128 value) noexcept {
    return value.lo ^ (value.hi + 0X9E3779B97F4A7C15ULL + (value.lo << 6) + (value.lo >> 2));
}

// AMC's canonical text hash, reproduced here as constexpr so the runtime can
// derive the same identities the AMC generator derives. The `lo` seed is the
// shared `fnv1a_offset_basis`; the `hi` lane is FNV prime + domain and uses
// XXH64's PRIME64_2 as its multiplier. The algorithm, the constants and the
// domain are a cross-tool contract: changing any of them changes every
// artifact. This must stay byte-for-byte identical to `AMC::hash_text`
// (AMC/src/Core/Core.cpp); `amc-core-test` pins it with fixed vectors.
struct CanonicalState {
    uint64_t lo;
    uint64_t hi;
};

constexpr CanonicalState canonical_hash_begin(uint64_t domain) noexcept {
    return {fnv1a_offset_basis ^ domain, fnv1a_prime + domain};
}

constexpr CanonicalState canonical_hash_byte(CanonicalState state, uint8_t byte) noexcept {
    state.lo = (state.lo ^ byte) * fnv1a_prime;
    state.hi = (state.hi ^ (byte + 31)) * canonical_hi_prime;
    return state;
}

constexpr CanonicalState canonical_hash_bytes(CanonicalState state, const char *data, size_t size) noexcept {
    for (size_t i = 0; i < size; ++i)
        state = canonical_hash_byte(state, static_cast<uint8_t>(data[i]));
    return state;
}

constexpr Hash128 canonical_hash(const char *data, size_t size, uint64_t domain) noexcept {
    const CanonicalState state = canonical_hash_bytes(canonical_hash_begin(domain), data, size);
    return {state.lo, state.hi};
}

template<size_t Capacity = 4>
class ValueSet {
public:
    bool add(const Value &value) noexcept {
        if (!valid(value.descriptor)) return false;
        for (size_t i = 0; i < _size; ++i) {
            if (same_descriptor(_values[i].descriptor, value.descriptor)) {
                _values[i] = value;
                return true;
            }
        }
        if (_size == Capacity) return false;
        _values[_size++] = value;
        return true;
    }

    const Value *find(const Descriptor &descriptor) const noexcept {
        for (size_t i = 0; i < _size; ++i)
            if (same_descriptor(_values[i].descriptor, descriptor)) return &_values[i];
        return nullptr;
    }

    size_t size() const noexcept { return _size; }

private:
    Value _values[Capacity == 0 ? 1 : Capacity]{};
    size_t _size = 0;
};
}   // namespace util

}   // namespace skl::abix

#endif   // SKL_ABIX_HASH_H
