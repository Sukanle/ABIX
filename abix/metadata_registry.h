/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */
#ifndef SKL_ABIX_METADATA_REGISTRY_H
#define SKL_ABIX_METADATA_REGISTRY_H

#include <stddef.h>
#include <stdint.h>

#include "abi_model.h"
#include "bootstrap.h"

namespace skl::abix::runtime {

enum class RegisterStatus : uint32_t {
    ok = 0,
    invalid_descriptor,
    duplicate_type,
    capacity_exceeded,
};

struct BootstrapMetadata {
    const model::TypeDesc *desc;
    const model::TypeLayout *layout;
};

struct RegistryEntry {
    const model::TypeDesc *desc;
    const model::TypeLayout *layout;
    uint64_t bootstrap_hash;
};

template<size_t Capacity = 64>
class MetadataRegistry {
public:
    using Entry = RegistryEntry;

    RegisterStatus register_type(const model::TypeDesc *desc,
                                 const model::TypeLayout *layout,
                                 uint64_t bootstrap_hash = 0) noexcept {
        if (!valid_descriptor(desc, layout))
            return RegisterStatus::invalid_descriptor;
        if (find_by_id(desc->id) != nullptr)
            return RegisterStatus::duplicate_type;
        if (_count == Capacity)
            return RegisterStatus::capacity_exceeded;
        _entries[_count++] = {desc, layout, bootstrap_hash};
        return RegisterStatus::ok;
    }

    const Entry *find_by_id(model::TypeId id) const noexcept {
        for (size_t i = 0; i < _count; ++i) {
            if (_entries[i].desc->id.lo == id.lo &&
                _entries[i].desc->id.hi == id.hi)
                return &_entries[i];
        }
        return nullptr;
    }

    const Entry *at(size_t index) const noexcept {
        return index < _count ? &_entries[index] : nullptr;
    }

    size_t size() const noexcept { return _count; }

    // The registry can bootstrap the metadata needed to describe its own
    // entry format. This is initialization-time only and remains single-threaded.
    bootstrap::Status bootstrap_self() noexcept {
        static const model::TypeDesc entry_desc{{0x524547454e545259ULL, 0x1}, 0, 0, 0};
        static const model::TypeLayout entry_layout{
            static_cast<uint32_t>(sizeof(RegistryEntry)),
            static_cast<uint32_t>(alignof(RegistryEntry)), 0, 0, {0x1, 0x1}};
        static const BootstrapMetadata entry_metadata{&entry_desc, &entry_layout};

        static const model::TypeDesc metadata_desc{{0x424f4f5453545241ULL, 0x1}, 0, 0, 0};
        static const model::TypeLayout metadata_layout{
            static_cast<uint32_t>(sizeof(BootstrapMetadata)),
            static_cast<uint32_t>(alignof(BootstrapMetadata)), 0, 0, {0x2, 0x1}};
        static const BootstrapMetadata metadata_metadata{&metadata_desc, &metadata_layout};

        const bootstrap::BootstrapRecord records[] = {
            {0x524547454e545259ULL, sizeof(RegistryEntry), alignof(RegistryEntry), &entry_metadata},
            {0x424f4f5453545241ULL, sizeof(BootstrapMetadata), alignof(BootstrapMetadata), &metadata_metadata},
        };
        const bootstrap::BootstrapImage image{
            records, 2, bootstrap::BOOTSTRAP_FORMAT_VERSION,
            bootstrap::BOOTSTRAP_LITTLE_ENDIAN, consume_bootstrap, this};
        return bootstrap::abix_bootstrap(image);
    }

    static bool valid_descriptor(const model::TypeDesc *desc,
                                 const model::TypeLayout *layout) noexcept {
        if (!desc || !layout || (desc->id.lo == 0 && desc->id.hi == 0))
            return false;
        if (layout->size == 0 || layout->align == 0)
            return false;
        return (layout->align & (layout->align - 1)) == 0;
    }

    static void consume_bootstrap(const bootstrap::BootstrapRecord *records,
                                  uint32_t count, void *context) noexcept {
        auto *registry = static_cast<MetadataRegistry *>(context);
        for (uint32_t i = 0; i < count; ++i) {
            const auto *metadata =
                static_cast<const BootstrapMetadata *>(records[i].metadata);
            if (metadata)
                registry->register_type(metadata->desc, metadata->layout,
                                        records[i].type_hash);
        }
    }

private:
    Entry _entries[Capacity == 0 ? 1 : Capacity]{};
    size_t _count = 0;
};

} // namespace skl::abix::runtime

#endif // SKL_ABIX_METADATA_REGISTRY_H
