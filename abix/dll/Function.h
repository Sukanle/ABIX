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
    }
    FunctionCC &operator=(FunctionCC &&o) noexcept {
        if (this != &o) {
            release_lib();
            _control = static_cast<SharedPtr<Control> &&>(o._control);
            _name = o._name;
            _name_hash = o._name_hash;
            _index = o._index;
            _valid = o._valid;
            o._name = nullptr;
            o._name_hash = 0;
            o._index = ~runtime::index_t{0};
            o._valid = false;
        }
        return *this;
    }

    void resolve(Object &lib, const char *name, runtime::version_t ver = 0) noexcept {
        release_lib();
        _control = lib.control();
        _name = name;
        _name_hash = mics::utils::cstr64(name);
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
        runtime::LookupResult r = find_index(*t, image->index, name, _sig, ver, idx);
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
        return _valid && _control
            && util::load_acquire(&_control->state) == static_cast<uint32_t>(rcu::State::active)
            && util::load_acquire(&_control->image) != nullptr;
    }
    explicit operator bool() const noexcept { return valid(); }
    runtime::index_t index() const noexcept { return _index; }
    uint64_t handle_id() const noexcept { return static_cast<uint64_t>(_index); }

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
        if (!_control || util::load_acquire(&_control->state) != static_cast<uint32_t>(rcu::State::active)) {
            last_error() = CallError::not_loaded;
            return default_ret();
        }
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
    }

    SharedPtr<Control> _control;
    const char *_name = nullptr;
    runtime::name_hash_t _name_hash = 0;
    runtime::index_t _index = ~runtime::index_t{0};
    bool _valid = false;
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
