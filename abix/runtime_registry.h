/*
 * Copyright 2026 Sukanle(https://github.com/Sukanle)
 *
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef SKL_ABIX_RUNTIME_REGISTRY_H
#define SKL_ABIX_RUNTIME_REGISTRY_H

#include <stddef.h>
#include <stdint.h>

#include "metadata_registry.h"
#include "runtime_descriptor.h"

namespace skl::abix::runtime {

// Generated projections may specialize this trait for their native C++ type.
// Keeping the primary template incomplete makes an unregistered type a
// compile-time error rather than a name-based runtime fallback.
template <typename T>
struct TypeTraits;

enum class RuntimeRegisterStatus : uint32_t {
    ok = 0,
    invalid_module,
    duplicate_type,
    capacity_exceeded,
    unknown_type_reference,
};

struct RuntimeRegistryEntry {
    const TypeDescriptor *descriptor;
    const model::TypeDesc *canonical;
    const model::TypeLayout *layout;
};

template <size_t Capacity = 64>
class RuntimeRegistry {
public:
    using CanonicalRegistry = MetadataRegistry<Capacity>;

    RuntimeRegisterStatus register_module(const ModuleDescriptor &module) noexcept {
        if (!valid(module)) return RuntimeRegisterStatus::invalid_module;
        if (module.type_count > Capacity - _count) return RuntimeRegisterStatus::capacity_exceeded;

        // Preflight the complete module before mutating either registry. This
        // makes module registration atomic from the caller's perspective.
        for (uint32_t i = 0; i < module.type_count; ++i) {
            const auto &type = module.types[i];
            if (find_by_id(type.type_id) || duplicate_in_module(module, i))
                return RuntimeRegisterStatus::duplicate_type;
            if (!valid_layout(type)) return RuntimeRegisterStatus::invalid_module;
        }
        for (uint32_t i = 0; i < module.type_count; ++i) {
            const auto &type = module.types[i];
            for (uint32_t j = 0; j < type.field_count; ++j)
                if (!known_type(module, type.fields[j].type_id))
                    return RuntimeRegisterStatus::unknown_type_reference;
        }
        for (uint32_t i = 0; i < module.function_count; ++i) {
            const auto &function = module.functions[i];
            if (!known_type(module, function.return_type))
                return RuntimeRegisterStatus::unknown_type_reference;
            for (uint32_t j = 0; j < function.parameter_count; ++j)
                if (!known_type(module, function.parameters[j].type_id))
                    return RuntimeRegisterStatus::unknown_type_reference;
        }

        for (uint32_t i = 0; i < module.type_count; ++i) {
            const auto &type = module.types[i];
            const size_t slot = _count + i;
            _canonical_desc[slot] = {type.type_id, type.flags, 0, static_cast<uint32_t>(slot)};
            _canonical_layout[slot] = {type.size, type.align, 0, type.field_count, type.layout_hash};
            const auto status = _canonical.register_type(&_canonical_desc[slot], &_canonical_layout[slot]);
            if (status != RegisterStatus::ok) return RuntimeRegisterStatus::invalid_module;
            _entries[slot] = {&type, &_canonical_desc[slot], &_canonical_layout[slot]};
        }
        _count += module.type_count;
        return RuntimeRegisterStatus::ok;
    }

    const RuntimeRegistryEntry *find_by_id(model::TypeId id) const noexcept {
        for (size_t i = 0; i < _count; ++i) {
            if (_entries[i].descriptor->type_id == id) return &_entries[i];
        }
        return nullptr;
    }

    const RuntimeRegistryEntry *find_by_name(const char *name) const noexcept {
        if (!name) return nullptr;
        for (size_t i = 0; i < _count; ++i) {
            const char *candidate = _entries[i].descriptor->name;
            const char *a = candidate;
            const char *b = name;
            while (*a && *b && *a == *b) { ++a; ++b; }
            if (*a == '\0' && *b == '\0') return &_entries[i];
        }
        return nullptr;
    }

    template <typename T>
    const RuntimeRegistryEntry *type_of() const noexcept {
        return find_by_id(TypeTraits<T>::type_id);
    }

    const RuntimeRegistryEntry *at(size_t index) const noexcept {
        return index < _count ? &_entries[index] : nullptr;
    }

    size_t size() const noexcept { return _count; }
    const CanonicalRegistry &canonical() const noexcept { return _canonical; }

private:
    bool known_type(const ModuleDescriptor &module, model::TypeId id) const noexcept {
        for (uint32_t i = 0; i < module.type_count; ++i)
            if (module.types[i].type_id == id) return true;
        return find_by_id(id) != nullptr;
    }

    bool duplicate_in_module(const ModuleDescriptor &module, uint32_t index) const noexcept {
        for (uint32_t i = 0; i < index; ++i)
            if (module.types[i].type_id == module.types[index].type_id) return true;
        return false;
    }

    static bool valid_layout(const TypeDescriptor &type) noexcept {
        return (type.align & (type.align - 1)) == 0 && type.align != 0;
    }

    RuntimeRegistryEntry _entries[Capacity == 0 ? 1 : Capacity]{};
    model::TypeDesc _canonical_desc[Capacity == 0 ? 1 : Capacity]{};
    model::TypeLayout _canonical_layout[Capacity == 0 ? 1 : Capacity]{};
    CanonicalRegistry _canonical{};
    size_t _count = 0;
};

}  // namespace skl::abix::runtime

#endif  // SKL_ABIX_RUNTIME_REGISTRY_H
