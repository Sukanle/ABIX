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
#ifndef SKL_ABIX_OBJ_DLL_H
#define SKL_ABIX_OBJ_DLL_H
#include "type.h"
#include "register.h"
#if SKL_ABIX_WINDOWS
#  include <libloaderapi.h>
#endif

SKL_ABIX_NAMESPACE_BEGIN

#if SKL_ABIX_WINDOWS
using module_handle = HMODULE;
#else
#  include <dlfcn.h>
using module_handle = void *;
#endif

enum class call_error : uint8_t {
    none = 0,
    not_loaded = 1,
    not_found = 2,
    sig_mismatch = 3,
    version_mismatch = 4,
    stale_handle = 5,
    table_changed = 6,
    invalid = 7,
    load_failed = 8,
};
inline call_error &last_error() noexcept {
    static call_error e = call_error::none;
    return e;
}

class dll_object;

namespace detail {
inline module_handle load_module(const char *path) noexcept {
#if SKL_ABIX_WINDOWS
    return LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}
inline void *resolve_symbol(module_handle m, const char *name) noexcept {
    if (!m) return nullptr;
#if SKL_ABIX_WINDOWS
    return reinterpret_cast<void *>(GetProcAddress(m, name));
#else
    return dlsym(m, name);
#endif
}
inline void unload_module(module_handle m) noexcept {
    if (m) {
#if SKL_ABIX_WINDOWS
        FreeLibrary(m);
#else
        dlclose(m);
#endif
    }
}
}   // namespace detail

class dll_object {
public:
    dll_object() noexcept = default;
    ~dll_object() { unload(); }
    dll_object(const dll_object &) = delete;
    dll_object &operator=(const dll_object &) = delete;

    bool load(const char *path) noexcept {
        unload();
        module_handle m = detail::load_module(path);
        if (!m) {
            last_error() = call_error::load_failed;
            return false;
        }
        auto getter = reinterpret_cast<const table *(*)(void)>(detail::resolve_symbol(m, SKL_ABIX_TABLE_GETTER));
        if (!getter) {
            detail::unload_module(m);
            last_error() = call_error::load_failed;
            return false;
        }
        const table *t = getter();
        if (!t || t->magic != SKL_ABIX_TABLE_MAGIC || t->format_version != SKL_ABIX_TABLE_FORMAT_VERSION) {
            detail::unload_module(m);
            last_error() = call_error::load_failed;
            return false;
        }
        _module = m;
        _table = t;
        _refs = 0;
        last_error() = call_error::none;
        return true;
    }

    bool unload() noexcept {
        if (_refs > 0) {
            last_error() = call_error::stale_handle;
            return false;
        }
        return unload_internal(true);
    }

    void force_unload() noexcept { unload_internal(false); }

    bool reload(const char *path) noexcept {
        unload_internal(false);
        return load(path);
    }

    bool is_loaded() const noexcept { return _module != nullptr && _table != nullptr; }
    const table *get_table() const noexcept { return _table; }
    module_handle module() const noexcept { return _module; }

    uint32_t ref_count() const noexcept { return _refs; }
    void add_ref() noexcept { ++_refs; }
    void release_ref() noexcept {
        if (_refs > 0) --_refs;
    }

private:
    bool unload_internal(bool report) noexcept {
        if (_module) {
            detail::unload_module(_module);
            _module = nullptr;
            _table = nullptr;
            _refs = 0;
        }
        if (report) last_error() = call_error::none;
        return true;
    }

    module_handle _module = nullptr;
    const table *_table = nullptr;
    uint32_t _refs = 0;
};

SKL_ABIX_NAMESPACE_END

#endif
