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
#ifndef SKL_ABIX_FN_DLL_H
#define SKL_ABIX_FN_DLL_H

#include "search.h"    // IWYU pragma: keep
#include "fn_sig.h"    // IWYU pragma: keep
#include "obj_dll.h"   // IWYU pragma: keep

SKL_ABIX_NAMESPACE_BEGIN
enum class AbiLookupPolicy : uint8_t {
    Linear = 0,
    StaticHot = 1,
    AdaptiveHot = 2,
};

template<cc::tag C, typename Sig, AbiLookupPolicy Policy = AbiLookupPolicy::Linear>
class dll_func_cc;

template<cc::tag C, typename R, typename... Args, AbiLookupPolicy Policy>
class dll_func_cc<C, R(Args...), Policy> {
    static constexpr sig_t _sig = fn_sig<R(Args...), C>::value;
    using fn_type = R (*)(Args...);

public:
    static constexpr AbiLookupPolicy lookup_policy = Policy;

    dll_func_cc() noexcept = default;
    dll_func_cc(dll_object &lib, const char *name, version_t ver = 0) noexcept { resolve(lib, name, ver); }
    ~dll_func_cc() noexcept { release_lib(); }
    dll_func_cc(const dll_func_cc &) = delete;
    dll_func_cc &operator=(const dll_func_cc &) = delete;
    dll_func_cc(dll_func_cc &&o) noexcept
        : _lib(o._lib)
        , _name(o._name)
        , _name_hash(o._name_hash)
        , _index(o._index)
        , _valid(o._valid) {
        o._lib = nullptr;
        o._name = nullptr;
        o._name_hash = 0;
        o._index = ~index_t{0};
        o._valid = false;
    }
    dll_func_cc &operator=(dll_func_cc &&o) noexcept {
        if (this != &o) {
            release_lib();
            _lib = o._lib;
            _name = o._name;
            _name_hash = o._name_hash;
            _index = o._index;
            _valid = o._valid;
            o._lib = nullptr;
            o._name = nullptr;
            o._name_hash = 0;
            o._index = ~index_t{0};
            o._valid = false;
        }
        return *this;
    }

    void resolve(dll_object &lib, const char *name, version_t ver = 0) noexcept {
        release_lib();
        _lib = &lib;
        _name = name;
        _name_hash = Reflect::Utils::cstr64(name);
        _index = ~index_t{0};
        _valid = false;
        if (!lib.is_loaded()) {
            last_error() = call_error::not_loaded;
            return;
        }
        index_t idx = ~index_t{0};
        lookup_result r = lookup_result::bad_table;
        if constexpr (Policy == AbiLookupPolicy::Linear) {
            r = find_index(*lib.get_table(), name, _sig, ver, idx);
        } else {
            r = find_index(*lib.get_table(), name, _sig, ver, idx);
        }
        switch (r) {
            case lookup_result::ok:
                _index = idx;
                _valid = true;
                lib.add_ref();
                last_error() = call_error::none;
                break;
            case lookup_result::sig_mismatch:
                _index = idx;
                last_error() = call_error::sig_mismatch;
                break;
            case lookup_result::version_mismatch: last_error() = call_error::version_mismatch; break;
            case lookup_result::not_found:        last_error() = call_error::not_found; break;
            default:                              last_error() = call_error::load_failed; break;
        }
    }

    bool valid() const noexcept { return _valid && _lib && _lib->is_loaded(); }
    explicit operator bool() const noexcept { return valid(); }
    index_t index() const noexcept { return _index; }
    const dll_object *library() const noexcept { return _lib; }
    uint64_t handle_id() const noexcept { return static_cast<uint64_t>(_index); }

    fn_type raw() const noexcept {
        if (!valid()) return nullptr;
        const entry &e = _lib->get_table()->entries[_index];
        fn_type fn = nullptr;
        memcpy(&fn, &e.fnptr, sizeof(fn));
        return fn;
    }

    R operator()(Args... args) const noexcept {
        if (!_lib || !_lib->is_loaded()) {
            last_error() = call_error::not_loaded;
            return default_ret();
        }
        if (!_lib->try_enter_read()) {
            return default_ret();
        }
        const table *t = _lib->get_table();
        if (_index >= t->count) {
            _lib->exit_read();
            last_error() = call_error::table_changed;
            return default_ret();
        }
        const entry &e = t->entries[_index];
        if (e.name_hash != _name_hash || e.sig != _sig || strcmp(e.name, _name) != 0) {
            _lib->exit_read();
            last_error() = call_error::table_changed;
            return default_ret();
        }
        if (e.fnptr == 0) {
            _lib->exit_read();
            last_error() = call_error::invalid;
            return default_ret();
        }
        fn_type fn = nullptr;
        memcpy(&fn, &e.fnptr, sizeof(fn));
        last_error() = call_error::none;
        if constexpr (std::is_void_v<R>) {
            fn(static_cast<Args>(args)...);
            _lib->exit_read();
        } else {
            R ret = fn(static_cast<Args>(args)...);
            _lib->exit_read();
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
    void release_lib() noexcept {
        if (_lib && _valid) _lib->release_ref();
        _lib = nullptr;
        _name = nullptr;
        _name_hash = 0;
        _index = ~index_t{0};
        _valid = false;
    }

    dll_object *_lib = nullptr;
    const char *_name = nullptr;
    name_hash_t _name_hash = 0;
    index_t _index = ~index_t{0};
    bool _valid = false;
};

template<typename Sig, cc::tag C = SKL_ABIX_CCPICK(Cdecl), AbiLookupPolicy Policy = AbiLookupPolicy::Linear>
class dll_func : public dll_func_cc<C, Sig, Policy> {
    using base_t = dll_func_cc<C, Sig, Policy>;

public:
    dll_func() noexcept = default;
    dll_func(dll_object &lib, const char *name, version_t ver = 0) noexcept { this->base_t::resolve(lib, name, ver); }
    dll_func(dll_func &&) noexcept = default;
    dll_func &operator=(dll_func &&) noexcept = default;
    dll_func(const dll_func &) = delete;
    dll_func &operator=(const dll_func &) = delete;
};

SKL_ABIX_NAMESPACE_END
#endif