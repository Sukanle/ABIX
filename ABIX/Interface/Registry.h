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
#ifndef SKL_ABIX_INTERFACE_REGISTRY_H
#define SKL_ABIX_INTERFACE_REGISTRY_H

#include <stddef.h>

#include <atomic>

#include "ABIX/Interface/Contract.h"

namespace skl::abix::poly {

// Binding-time registry of interface implementations. Adding and resolving are
// setup work: a linear scan is fine because it never runs on the call path.
// Capacity is fixed and allocation-free, like the type Registry.
//
// Threading: the registry is not internally synchronized. `add()` must be
// serialized by the caller, and no `add()` may race a `find()`. Once the
// registry has been populated and is no longer mutated, concurrent `find()`
// calls are safe; the diagnostic lookup counter is a relaxed atomic so it does
// not introduce a data race on that read-only path.
template<size_t Capacity = 64>
class InterfaceRegistry {
public:
    InterfaceStatus add(const InterfaceImplementation &impl) noexcept {
        if (impl.interface_id == InterfaceId{}
            || impl.concrete_type == model::TypeId{}
            || (impl.method_count != 0 && impl.methods == nullptr))
            return InterfaceStatus::invalid_table;
        // A repeated (concrete_type, interface_id, version) is a conflict, not a
        // silent overwrite: two providers claim the same binding.
        if (find_exact(impl.concrete_type, impl.interface_id, impl.version) != nullptr)
            return InterfaceStatus::duplicate_implementation;
        if (_count == Capacity) return InterfaceStatus::capacity_exceeded;
        _entries[_count++] = impl;
        return InterfaceStatus::ok;
    }

    // Resolve a binding. `version == 0` means "any version" and selects the
    // newest registered one; a non-zero version requires an exact match.
    const InterfaceImplementation *find(
        model::TypeId concrete_type, InterfaceId interface_id, uint32_t version = 0) const noexcept {
        _lookups.fetch_add(1, std::memory_order_relaxed);
        if (version == 0) return find_newest(concrete_type, interface_id);
        return find_exact(concrete_type, interface_id, version);
    }

    // Newest registered version for (concrete_type, interface_id), or nullptr.
    const InterfaceImplementation *find_newest(model::TypeId concrete_type, InterfaceId interface_id) const noexcept {
        const InterfaceImplementation *best = nullptr;
        for (size_t i = 0; i < _count; ++i) {
            const auto &entry = _entries[i];
            if (entry.concrete_type != concrete_type || entry.interface_id != interface_id) continue;
            if (best == nullptr || entry.version >= best->version) best = &entry;
        }
        return best;
    }

    // Does any version of (concrete_type, interface_id) exist? Reports the
    // newest so a caller can distinguish "unknown interface" from "wrong
    // version" without a second full scan.
    bool available(model::TypeId concrete_type, InterfaceId interface_id, uint32_t &newest) const noexcept {
        const auto *entry = find_newest(concrete_type, interface_id);
        if (entry == nullptr) return false;
        newest = entry->version;
        return true;
    }

    size_t size() const noexcept { return _count; }
    const InterfaceImplementation *at(size_t index) const noexcept {
        return index < _count ? &_entries[index] : nullptr;
    }
    // Diagnostic only: how many find() calls this registry has served. Lets a
    // test prove that resolve-then-call performs exactly one lookup. The
    // counter is relaxed: it is not a synchronization or ordering primitive.
    size_t lookup_count() const noexcept { return _lookups.load(std::memory_order_relaxed); }

private:
    const InterfaceImplementation *find_exact(
        model::TypeId concrete_type, InterfaceId interface_id, uint32_t version) const noexcept {
        for (size_t i = 0; i < _count; ++i) {
            const auto &entry = _entries[i];
            if (entry.concrete_type == concrete_type && entry.interface_id == interface_id && entry.version == version)
                return &entry;
        }
        return nullptr;
    }

    InterfaceImplementation _entries[Capacity == 0 ? 1 : Capacity]{};
    size_t _count = 0;
    mutable std::atomic<size_t> _lookups{0};
};

}   // namespace skl::abix::poly

#endif   // SKL_ABIX_INTERFACE_REGISTRY_H
