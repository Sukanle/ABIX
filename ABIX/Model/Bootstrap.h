/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */
#ifndef SKL_ABIX_MODEL_BOOTSTRAP_H
#define SKL_ABIX_MODEL_BOOTSTRAP_H

#include <stdint.h>

namespace skl::abix::model {

#define SKL_ABIX_BOOTSTRAP_FORMAT_VERSION 0X1U
#define SKL_ABIX_BOOTSTRAP_LITTLE_ENDIAN 0X1U

// These records intentionally use only fixed-width fields and a pointer to
// immutable metadata. They can be emitted into .rodata by a generator.
struct BootRecord {
    uint64_t type_hash;
    uint32_t size;
    uint32_t align;
    const void *metadata;
};

// Serialized form of a record. Pointers are deliberately excluded from the
// wire format; a loader resolves metadata from an artifact-local offset.
struct WireRecord {
    uint64_t type_hash;
    uint32_t size;
    uint32_t align;
};

using Consumer = void (*)(const BootRecord *, uint32_t, void *);

struct BootImage {
    const BootRecord *records;
    uint32_t count;
    uint32_t format_version;
    uint32_t byte_order;
    Consumer consume;
    void *context;
};

enum class BootStatus : uint32_t {
    ok = 0,
    invalid_image,
    unsupported_format,
    unsupported_byte_order,
};

static_assert(sizeof(WireRecord) == 16, "Bootstrap wire record layout changed");
static_assert(sizeof(BootRecord) == sizeof(void *) + 16, "Bootstrap in-process record layout changed");

constexpr bool valid_record(const BootRecord &record) noexcept {
    return record.type_hash != 0 && record.size != 0 && record.align != 0 && record.metadata != nullptr;
}

inline BootStatus abix_bootstrap(const BootImage &image) noexcept {
    if (image.format_version != SKL_ABIX_BOOTSTRAP_FORMAT_VERSION) return BootStatus::unsupported_format;
    if (image.byte_order != SKL_ABIX_BOOTSTRAP_LITTLE_ENDIAN) return BootStatus::unsupported_byte_order;
    if (image.count != 0 && image.records == nullptr) return BootStatus::invalid_image;
    if (image.consume == nullptr) return BootStatus::invalid_image;
    for (uint32_t i = 0; i < image.count; ++i) {
        if (!valid_record(image.records[i])) return BootStatus::invalid_image;
    }
    image.consume(image.records, image.count, image.context);
    return BootStatus::ok;
}

}   // namespace skl::abix::model

#endif   // SKL_ABIX_BOOTSTRAP_H
