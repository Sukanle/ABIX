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
#ifndef SKL_ABIX_INTERFACE_OWNER_H
#define SKL_ABIX_INTERFACE_OWNER_H

#include <stdint.h>

#include "ABIX/Interface/Lease.h"

// ABIX Interface layer — the owner ABI.
//
// Interface ABI answers "through what contract do I call this object"; the
// owner ABI answers "who owns it, who releases it, and where does the releasing
// code live". They are separate contracts.
//
// Rules this encodes:
//   * release must return to the creator's allocation domain — a consumer must
//     never free() or delete the pointer, and must never infer a deallocator
//     from the address or the layout;
//   * an object handle keeps the creating module loaded for as long as it
//     lives, so a long-lived object cannot outlive its destroy entry.
//
// Only three modes are formalised: borrowed, owned and copied. Shared/weak
// ownership is deliberately out of scope.

namespace skl::abix::owner {

using skl::abix::poly::acquire_lease;
using skl::abix::poly::invalid_module_instance;
using skl::abix::poly::ModuleControl;
using skl::abix::poly::ModuleInstanceId;
using skl::abix::poly::ModuleLease;

constexpr uint32_t abix_owner_abi_version = 1;

enum class mode : uint32_t {
    borrowed = 0,   // non-owning reference; the borrower must not release it
    owned = 1,      // exclusive ownership transferred; released through destroy
    copied = 2,     // independent copy; released through its own destroy
};

constexpr const char *to_string(mode value) noexcept {
    switch (value) {
        case mode::borrowed: return "borrowed";
        case mode::owned:    return "owned";
        case mode::copied:   return "copied";
    }
    return "unknown";
}

// The creator's destruction entry, with a fixed C ABI. The creator owns both the
// allocation and the deallocator; the consumer only calls this.
using destroy_fn = void (*)(void *object);

// Opaque owner handle. POD and boundary-stable. `object` is opaque to the
// consumer; `destroy` is the creator's release binding (required for owned,
// optional for copied); `module_instance` is diagnostic (which load created it).
struct handle {
    void *object;               //  8
    uint32_t mode;              //  4
    uint32_t flags;             //  4
    destroy_fn destroy;         //  8
    uint64_t module_instance;   //  8
};
static_assert(sizeof(handle) == 32, "owner::handle wire layout changed");

inline bool valid(const handle &value) noexcept { return value.object != nullptr; }

inline handle make_borrowed(void *object) noexcept {
    return {object, static_cast<uint32_t>(mode::borrowed), 0, nullptr, invalid_module_instance};
}
inline handle make_owned(
    void *object, destroy_fn destroy, ModuleInstanceId module_instance = invalid_module_instance) noexcept {
    return {object, static_cast<uint32_t>(mode::owned), 0, destroy, module_instance};
}
inline handle make_copied(
    void *object, destroy_fn destroy, ModuleInstanceId module_instance = invalid_module_instance) noexcept {
    return {object, static_cast<uint32_t>(mode::copied), 0, destroy, module_instance};
}

// Runtime owner handle: the creator's POD handle plus a module lease that keeps
// the destroying module loaded for as long as the object lives.
//
// A creation that fails returns an invalid handle (object == nullptr) rather
// than a partial one. `destroy` returns void: a release cannot fail, so any
// failure must be reported at creation time.
class ref {
public:
    ref() noexcept = default;
    ref(const ref &) = delete;
    ref &operator=(const ref &) = delete;
    ref(ref &&other) noexcept
        : _handle(other._handle)
        , _lease(static_cast<ModuleLease &&>(other._lease)) {
        other._handle = {};
    }
    ref &operator=(ref &&other) noexcept {
        if (this != &other) {
            release();
            _handle = other._handle;
            _lease = static_cast<ModuleLease &&>(other._lease);
            other._handle = {};
        }
        return *this;
    }
    ~ref() { release(); }

    // Adopt a creator-returned handle. For an owned/copied handle `control` is
    // the creating module's lifecycle control; the lease keeps it loaded until
    // the object is released. A borrowed handle may omit the lease.
    static ref adopt(const handle &value, ModuleControl *control = nullptr) noexcept {
        ref result;
        result._handle = value;
        if (control != nullptr) result._lease = acquire_lease(*control);
        return result;
    }

    static ref borrow(void *object, ModuleControl *control = nullptr) noexcept {
        return adopt(make_borrowed(object), control);
    }

    bool valid() const noexcept { return _handle.object != nullptr; }
    void *object() const noexcept { return _handle.object; }
    mode ownership() const noexcept { return static_cast<mode>(_handle.mode); }
    bool owns() const noexcept { return ownership() == mode::owned || ownership() == mode::copied; }
    bool leased() const noexcept { return _lease.valid(); }
    const handle &raw() const noexcept { return _handle; }

    // Release exactly once: for an owned/copied handle call the creator's
    // destroy entry, then drop the module lease. A borrowed handle is not
    // destroyed (the borrower never owned it). Releasing twice is a no-op
    // because the handle is cleared.
    void release() noexcept {
        if (_handle.object != nullptr && owns() && _handle.destroy != nullptr) _handle.destroy(_handle.object);
        _lease.release();
        _handle = {};
    }

private:
    handle _handle{};
    ModuleLease _lease;
};

}   // namespace skl::abix::owner

#endif   // SKL_ABIX_INTERFACE_OWNER_H
