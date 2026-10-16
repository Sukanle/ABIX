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
#ifndef SKL_ABIX_INTERFACE_LEASE_H
#define SKL_ABIX_INTERFACE_LEASE_H

#include <stdint.h>

#include <atomic>
#include <thread>

#include "ABIX/Interface/Module.h"

namespace skl::abix::poly {

// A module's lifecycle is a small state machine with a lease count:
//
//   loading ──activate──▶ active ──begin_unload──▶ draining ──idle──▶ unloaded
//
// A lease is held for the duration of an active call or a live object handle.
// `begin_unload` refuses new leases; the module may only actually unload once
// every lease is released. This is what makes a cached method binding safe:
// the binding stays valid exactly as long as the caller holds a lease.
//
// The count is deliberately simple (a refcount plus two states) rather than a
// lock-free registry: correctness first, and the transitions are the contract
// M3.6 (owner handles) reuses for long-lived objects.

enum class ModuleState : uint32_t {
    loading = 0,
    active,
    draining,
    unloaded,
};

constexpr const char *to_string(ModuleState state) noexcept {
    switch (state) {
        case ModuleState::loading:  return "loading";
        case ModuleState::active:   return "active";
        case ModuleState::draining: return "draining";
        case ModuleState::unloaded: return "unloaded";
    }
    return "unknown";
}

// Caller-owned lifecycle control for one loaded module instance. The loader
// (or the test harness) owns it and hands its address to the registry. Keeping
// it out of the registry lets the loader tie it to the platform module handle so
// a lease can hold an OS-level reference (see `pin`/`unpin` below).
struct ModuleControl {
    ModuleInstanceId id = invalid_module_instance;
    const char *name = nullptr;

    // Loader-owned: the platform handle (HMODULE / dlopen handle) and, on POSIX,
    // the load path (a dlopen handle alone cannot re-open an object, so the path
    // is needed to take a second OS reference). `pin_state` is scratch the
    // platform keeps between pin and unpin. The hooks run on the 0<->1 lease
    // transitions and may be null (the lease is then logical only).
    void *module_handle = nullptr;
    const char *module_path = nullptr;
    void *pin_state = nullptr;
    bool (*pin)(ModuleControl &control) = nullptr;
    void (*unpin)(ModuleControl &control) = nullptr;

    std::atomic<uint32_t> state{static_cast<uint32_t>(ModuleState::loading)};
    std::atomic<uint32_t> leases{0};
};

// A fresh instance id for every load. Monotonic; never reused.
inline ModuleInstanceId next_module_instance_id() noexcept {
    static std::atomic<uint64_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}

inline ModuleState module_state(const ModuleControl &control) noexcept {
    return static_cast<ModuleState>(control.state.load(std::memory_order_acquire));
}

inline bool activate_module(ModuleControl &control) noexcept {
    uint32_t expected = static_cast<uint32_t>(ModuleState::loading);
    return control.state.compare_exchange_strong(
        expected, static_cast<uint32_t>(ModuleState::active), std::memory_order_acq_rel);
}

// Acquire a lease only while the module is active. The state is re-checked
// after the increment so a concurrent begin_unload cannot be missed. The first
// lease takes an OS-level reference (`pin`) so the module stays mapped until the
// last lease is released.
inline bool try_acquire_lease(ModuleControl &control) noexcept {
    if (control.state.load(std::memory_order_acquire) != static_cast<uint32_t>(ModuleState::active)) return false;
    const uint32_t before = control.leases.fetch_add(1, std::memory_order_acq_rel);
    if (control.state.load(std::memory_order_acquire) != static_cast<uint32_t>(ModuleState::active)) {
        control.leases.fetch_sub(1, std::memory_order_acq_rel);
        return false;
    }
    // `before == 0` means no other lease exists, so no concurrent release can
    // race the pin.
    if (before == 0 && control.pin != nullptr) control.pin(control);
    return true;
}

// The last lease drops the OS-level reference, so the owner's eventual
// unload()/FreeLibrary()/dlclose() brings the last reference to zero and unmaps
// the module exactly once every lease is gone.
inline void release_lease(ModuleControl &control) noexcept {
    if (control.leases.fetch_sub(1, std::memory_order_acq_rel) == 1 && control.unpin != nullptr) control.unpin(control);
}

// Stop admitting new leases. Returns true when the module is draining (or was
// already unloaded); false when it was never activated.
inline bool begin_unload(ModuleControl &control) noexcept {
    uint32_t state = control.state.load(std::memory_order_acquire);
    for (;;) {
        if (state == static_cast<uint32_t>(ModuleState::draining)
            || state == static_cast<uint32_t>(ModuleState::unloaded))
            return true;
        if (state != static_cast<uint32_t>(ModuleState::active)) return false;
        if (control.state.compare_exchange_weak(
                state, static_cast<uint32_t>(ModuleState::draining), std::memory_order_acq_rel))
            return true;
    }
}

// Wait until every lease is released. Bounded so a leaked lease cannot hang a
// caller forever; the caller decides what to do with a false result.
inline bool wait_for_idle(const ModuleControl &control, uint32_t max_spins = 1'000'000) noexcept {
    for (uint32_t spin = 0; spin < max_spins; ++spin) {
        if (control.leases.load(std::memory_order_acquire) == 0) return true;
        std::this_thread::yield();
    }
    return control.leases.load(std::memory_order_acquire) == 0;
}

inline void mark_unloaded(ModuleControl &control) noexcept {
    control.state.store(static_cast<uint32_t>(ModuleState::unloaded), std::memory_order_release);
}

// RAII lease. Movable, non-copyable; releasing an unheld lease is a no-op.
class ModuleLease {
public:
    ModuleLease() noexcept = default;
    explicit ModuleLease(ModuleControl &control) noexcept
        : _control(&control) {}
    ModuleLease(const ModuleLease &) = delete;
    ModuleLease &operator=(const ModuleLease &) = delete;
    ModuleLease(ModuleLease &&other) noexcept
        : _control(other._control) {
        other._control = nullptr;
    }
    ModuleLease &operator=(ModuleLease &&other) noexcept {
        if (this != &other) {
            release();
            _control = other._control;
            other._control = nullptr;
        }
        return *this;
    }
    ~ModuleLease() { release(); }

    bool valid() const noexcept { return _control != nullptr; }
    void release() noexcept {
        if (_control != nullptr) {
            release_lease(*_control);
            _control = nullptr;
        }
    }

private:
    ModuleControl *_control = nullptr;
};

// Acquire a lease, returning an invalid handle (never a partial one) when the
// module is not active.
inline ModuleLease acquire_lease(ModuleControl &control) noexcept {
    if (!try_acquire_lease(control)) return ModuleLease();
    return ModuleLease(control);
}

}   // namespace skl::abix::poly

#endif   // SKL_ABIX_INTERFACE_LEASE_H
