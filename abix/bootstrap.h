/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */
#ifndef SKL_ABIX_BOOTSTRAP_H
#define SKL_ABIX_BOOTSTRAP_H

#include <stdint.h>

namespace skl::abix::bootstrap {

constexpr uint32_t BOOTSTRAP_FORMAT_VERSION = 1;
constexpr uint32_t BOOTSTRAP_LITTLE_ENDIAN = 1;

// These records intentionally use only fixed-width fields and a pointer to
// immutable metadata. They can be emitted into .rodata by a generator.
struct BootstrapRecord {
    uint64_t type_hash;
    uint32_t size;
    uint32_t align;
    const void *metadata;
};

// Serialized form of a record. Pointers are deliberately excluded from the
// wire format; a loader resolves metadata from an artifact-local offset.
struct BootstrapWireRecord {
    uint64_t type_hash;
    uint32_t size;
    uint32_t align;
};

using BootstrapConsumer = void (*)(const BootstrapRecord *, uint32_t, void *);

struct BootstrapImage {
    const BootstrapRecord *records;
    uint32_t count;
    uint32_t format_version;
    uint32_t byte_order;
    BootstrapConsumer consume;
    void *context;
};

enum class Status : uint32_t {
    ok = 0,
    invalid_image,
    unsupported_format,
    unsupported_byte_order,
};

static_assert(sizeof(BootstrapWireRecord) == 16,
              "Bootstrap wire record layout changed");
static_assert(sizeof(BootstrapRecord) == sizeof(void *) + 16,
              "Bootstrap in-process record layout changed");

constexpr bool valid_record(const BootstrapRecord &record) noexcept {
    return record.type_hash != 0 && record.size != 0 && record.align != 0 &&
           record.metadata != nullptr;
}

inline Status abix_bootstrap(const BootstrapImage &image) noexcept {
    if (image.format_version != BOOTSTRAP_FORMAT_VERSION)
        return Status::unsupported_format;
    if (image.byte_order != BOOTSTRAP_LITTLE_ENDIAN)
        return Status::unsupported_byte_order;
    if (image.count != 0 && image.records == nullptr)
        return Status::invalid_image;
    if (image.consume == nullptr)
        return Status::invalid_image;
    for (uint32_t i = 0; i < image.count; ++i) {
        if (!valid_record(image.records[i])) return Status::invalid_image;
    }
    image.consume(image.records, image.count, image.context);
    return Status::ok;
}

} // namespace skl::abix::bootstrap

#endif // SKL_ABIX_BOOTSTRAP_H
