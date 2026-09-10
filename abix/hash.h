/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */
#ifndef SKL_ABIX_HASH_H
#define SKL_ABIX_HASH_H

#include <stddef.h>
#include <stdint.h>

#include "abi_model.h"

namespace skl::abix::hash {

enum class Algorithm : uint16_t {
    fnv1a = 1,
    xxh3 = 2,
    blake3 = 3,
    sha256 = 4,
    fnv1a_dual_lane_reference = 0x10,
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
    model::Hash128 digest;
};

constexpr bool same_descriptor(const Descriptor &lhs,
                               const Descriptor &rhs) noexcept {
    return lhs.algorithm == rhs.algorithm &&
           lhs.algorithm_version == rhs.algorithm_version &&
           lhs.digest_bits == rhs.digest_bits && lhs.domain == rhs.domain;
}

constexpr bool same_value(const Value &lhs, const Value &rhs) noexcept {
    return same_descriptor(lhs.descriptor, rhs.descriptor) &&
           lhs.digest.lo == rhs.digest.lo && lhs.digest.hi == rhs.digest.hi;
}

constexpr bool valid(const Descriptor &descriptor) noexcept {
    return descriptor.algorithm_version != 0 && descriptor.digest_bits != 0 &&
           descriptor.digest_bits <= 128;
}

// The dual-lane value is an explicitly named transitional reference format.
// It is not the standard FNV-1a-128 algorithm and is not a security hash.
constexpr uint64_t fnv1a64(const uint8_t *data, size_t size,
                           uint64_t offset) noexcept {
    uint64_t result = offset;
    for (size_t i = 0; i < size; ++i) {
        result ^= data[i];
        result *= 1'099'511'628'211ULL;
    }
    return result;
}

constexpr Value fnv1a(const uint8_t *data, size_t size, Domain domain,
                      uint16_t bits = 128) noexcept {
    constexpr uint64_t offset_lo = 14'695'981'039'346'656'037ULL;
    constexpr uint64_t offset_hi = 10'951'620'151'779'511'277ULL;
    const uint8_t domain_byte = static_cast<uint8_t>(domain);
    const uint64_t lo = fnv1a64(data, size, offset_lo ^ domain_byte);
    const uint64_t hi = fnv1a64(data, size, offset_hi ^ (domain_byte << 8));
    const Algorithm algorithm = bits == 64
                                    ? Algorithm::fnv1a
                                    : Algorithm::fnv1a_dual_lane_reference;
    return {{algorithm, 1, bits, domain}, {lo, hi}};
}

constexpr uint64_t runtime_key(model::Hash128 value) noexcept {
    return value.lo ^ (value.hi + 0x9e3779b97f4a7c15ULL +
                       (value.lo << 6) + (value.lo >> 2));
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
            if (same_descriptor(_values[i].descriptor, descriptor))
                return &_values[i];
        return nullptr;
    }

    size_t size() const noexcept { return _size; }

private:
    Value _values[Capacity == 0 ? 1 : Capacity]{};
    size_t _size = 0;
};

} // namespace skl::abix::hash

#endif // SKL_ABIX_HASH_H
