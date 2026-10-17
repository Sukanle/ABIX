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
#ifndef SKL_ABIX_DLL_FUNCTION_H
#define SKL_ABIX_DLL_FUNCTION_H

#include "ABIX/Runtime/Search.h"
#include "ABIX/DLL/FnSig.h"
#include "ABIX/DLL/Object.h"
#include "ABIX/RCU/Domain.h"

namespace skl::abix::dll {

template<cc::tag C, typename Sig>
class FunctionCC;

template<cc::tag C, typename R, typename... Args>
class FunctionCC<C, R(Args...)> {
    static constexpr runtime::sig_t _sig = FnSig<R(Args...), C>::value;
    using FnPtr = R (*)(Args...);

public:
    FunctionCC() noexcept = default;
    FunctionCC(Object &lib, const char *name, runtime::version_t ver = 0) noexcept { resolve(lib, name, ver); }
    ~FunctionCC() noexcept { release_lib(); }
    FunctionCC(const FunctionCC &) = delete;
    FunctionCC &operator=(const FunctionCC &) = delete;
    FunctionCC(FunctionCC &&o) noexcept
        : _control(static_cast<SharedPtr<Control> &&>(o._control))
        , _name(o._name)
        , _name_hash(o._name_hash)
        , _index(o._index)
        , _valid(o._valid) {
        o._name = nullptr;
        o._name_hash = 0;
        o._index = ~runtime::index_t{0};
        o._valid = false;
        o._verified_image = nullptr;
    }
    FunctionCC &operator=(FunctionCC &&o) noexcept {
        if (this != &o) {
            release_lib();
            _control = static_cast<SharedPtr<Control> &&>(o._control);
            _name = o._name;
            _name_hash = o._name_hash;
            _index = o._index;
            _valid = o._valid;
            _verified_image = nullptr;
            o._name = nullptr;
            o._name_hash = 0;
            o._index = ~runtime::index_t{0};
            o._valid = false;
            o._verified_image = nullptr;
        }
        return *this;
    }

    void resolve(Object &lib, const char *name, runtime::version_t ver = 0) noexcept {
        release_lib();
        _control = lib.control();
        _name = name;
        // The table stores (and the index probes) the 32-bit folded name hash, so
        // cache that form explicitly: find_index() below takes it, which keeps the
        // hash from being recomputed on every resolve.
        _name_hash = runtime::name_hash(name);
        _index = ~runtime::index_t{0};
        _valid = false;
        if (!_control) {
            last_error() = CallError::load_failed;
            return;
        }

        rcu::LockGuard guard(rcu::Domain::instance());
        Image *image = util::load_acquire(&_control->image);
        if (!image) {
            last_error() = CallError::not_loaded;
            return;
        }
        const runtime::Table *t = image->export_table;
        if (!t) {
            last_error() = CallError::not_loaded;
            return;
        }

        runtime::index_t idx = ~runtime::index_t{0};
        runtime::LookupResult r = find_index(*t, image->index, name, _name_hash, _sig, ver, idx);
        switch (r) {
            case runtime::LookupResult::ok:
                _index = idx;
                _valid = true;
                last_error() = CallError::none;
                break;
            case runtime::LookupResult::sig_mismatch:
                _index = idx;
                last_error() = CallError::sig_mismatch;
                break;
            case runtime::LookupResult::version_mismatch: last_error() = CallError::version_mismatch; break;
            case runtime::LookupResult::not_found:        last_error() = CallError::not_found; break;
            default:                                      last_error() = CallError::load_failed; break;
        }
    }

    bool valid() const noexcept {
        return _valid
            && _control
            && util::load_acquire(&_control->state) == static_cast<uint32_t>(rcu::State::active)
            && util::load_acquire(&_control->image) != nullptr;
    }
    explicit operator bool() const noexcept { return valid(); }
    runtime::index_t index() const noexcept { return _index; }
    uint64_t handle_id() const noexcept { return static_cast<uint64_t>(_index); }

    // The pointer is handed out *after* the read-side section has been released, so
    // nothing ties it to a live image: it is only valid while the image it came from
    // is still the current one. A `reload()` (or `unload()` from another thread)
    // invalidates it. Use `operator()`, which revalidates on every call, or hold an
    // Interface lease, if the target has to stay valid across reloads.
    FnPtr raw() const noexcept {
        if (!valid()) return nullptr;
        rcu::LockGuard guard(rcu::Domain::instance());
        auto *image = util::load_acquire(&_control->image);
        if (!image || !image->export_table) return nullptr;
        if (_index >= image->export_table->count) return nullptr;
        const auto &e = image->export_table->entries[_index];
        FnPtr fn = nullptr;
        memcpy(&fn, &e.fnptr, sizeof(fn));
        return fn;
    }

    R operator()(Args... args) const noexcept {
        if (!_control) {
            last_error() = CallError::not_loaded;
            return default_ret();
        }

        // Fast path. Enter the read section first, so the module cannot be
        // reclaimed while the call runs, then confirm the cached snapshot with a
        // single relaxed load. `_index` is reused only when the current image is
        // the one it was validated against *and* the entry still carries the
        // resolved name hash and signature: that keeps this handle's contract
        // ("the function with this name and signature in this module") intact
        // while moving the per-call `strcmp` to the slow path. A stale or torn
        // snapshot can therefore only cost a revalidate, never a wrong call.
        //
        // The section is released *after* the call returns, so the callee must not
        // let an exception escape: an exception would skip the `exit()` below and
        // leave this thread inside its section for good, which blocks every later
        // grace period in the process. Calls cross a C ABI boundary, so that is the
        // contract rather than something the callee is expected to test for.
        {
            rcu::Domain::instance().enter();
            const Image *image = util::load_acquire(&_control->image);
            if (image != nullptr
                && image == util::load_relaxed(&_verified_image)
                && image->export_table != nullptr
                && _index < image->export_table->count) {
                const auto &entry = image->export_table->entries[_index];
                if (entry.name_hash == _name_hash && entry.sig == _sig && entry.fnptr != 0) {
                    FnPtr verified = nullptr;
                    memcpy(&verified, &entry.fnptr, sizeof(verified));
                    last_error() = CallError::none;
                    if constexpr (std::is_void_v<R>) {
                        verified(args...);
                        rcu::Domain::instance().exit();
                        return;
                    } else {
                        R result = verified(args...);
                        rcu::Domain::instance().exit();
                        return result;
                    }
                }
            }
            rcu::Domain::instance().exit();
        }

        // Slow path: full re-validation (state, table, bounds, hash + sig + name).
        if (util::load_acquire(&_control->state) != static_cast<uint32_t>(rcu::State::active)) {
            last_error() = CallError::not_loaded;
            return default_ret();
        }
        // Read the image *before* the table: the snapshot recorded below can then
        // only be older than the table it was validated against, and an image
        // that moved in between simply fails the pointer comparison next time.
        const Image *validated_against = util::load_acquire(&_control->image);
        const auto *t = enter_read();
        if (!t) return default_ret();
        if (_index >= t->count) {
            exit_read();
            last_error() = CallError::table_changed;
            return default_ret();
        }
        const auto &e = t->entries[_index];
        if (e.name_hash != _name_hash || e.sig != _sig || strcmp(e.name, _name) != 0) {
            exit_read();
            last_error() = CallError::table_changed;
            return default_ret();
        }
        if (e.fnptr == 0) {
            exit_read();
            last_error() = CallError::invalid;
            return default_ret();
        }
        FnPtr fn = nullptr;
        memcpy(&fn, &e.fnptr, sizeof(fn));
        util::store_relaxed(&_verified_image, validated_against);
        last_error() = CallError::none;
        if constexpr (std::is_void_v<R>) {
            fn(args...);
            exit_read();
        } else {
            R ret = fn(args...);
            exit_read();
            return ret;
        }
    }

private:
    static R default_ret() noexcept {
        if constexpr (std::is_void_v<R>)
            return;
        else
            return R{};
    }

    const runtime::Table *enter_read() const noexcept {
        rcu::Domain::instance().enter();
        auto *img = util::load_acquire(&_control->image);
        if (!img) {
            rcu::Domain::instance().exit();
            return nullptr;
        }
        return img->export_table;
    }
    void exit_read() const noexcept { rcu::Domain::instance().exit(); }

    void release_lib() noexcept {
        _control = SharedPtr<Control>{};
        _name = nullptr;
        _name_hash = 0;
        _index = ~runtime::index_t{0};
        _valid = false;
        _verified_image = nullptr;
    }

    SharedPtr<Control> _control;
    const char *_name = nullptr;
    runtime::name_hash_t _name_hash = 0;
    runtime::index_t _index = ~runtime::index_t{0};
    bool _valid = false;
    // `Image` against which `_index` was last validated, or nullptr. Only one
    // pointer is needed: `Object` swaps `_control->image` on every load, reload,
    // unload and force-unload, so an unchanged pointer means unchanged entries.
    // It is a hint, re-confirmed by the entry's name hash and signature before a
    // call, and `mutable` because `operator()` is const and may be called from
    // several threads. Access goes through the `util::` atomic helpers, and every
    // lifecycle path (resolve / move / release) clears it.
    mutable const Image *_verified_image = nullptr;
};

template<typename Sig, cc::tag C = SKL_ABIX_CCPICK(Cdecl)>
class Function : public FunctionCC<C, Sig> {
    using BaseType = FunctionCC<C, Sig>;

public:
    Function() noexcept = default;
    Function(Object &lib, const char *name, runtime::version_t ver = 0) noexcept { BaseType::resolve(lib, name, ver); }
    Function(Function &&) noexcept = default;
    Function &operator=(Function &&) noexcept = default;
    Function(const Function &) = delete;
    Function &operator=(const Function &) = delete;
};

}   // namespace skl::abix::dll
#endif
