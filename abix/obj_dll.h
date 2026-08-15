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
#include "rcu_config.h"
#include "rcu_domain.h"
#include "atomic.h"
#include "log.h"
#include <new>
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
    unloading = 9,
};
inline call_error &last_error() noexcept {
    static call_error e = call_error::none;
    return e;
}

class dll_object;

struct dll_image {
    module_handle module;
    const table *table;
};

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

inline void reclaim_image(void *p) noexcept {
    dll_image *img = static_cast<dll_image *>(p);
    unload_module(img->module);
    delete img;
}
}   // namespace detail

class dll_object {
public:
    dll_object() noexcept = default;
    explicit dll_object(const RCUTimeoutConfig &cfg) noexcept
        : _timeout_ms(cfg.timeout_ms)
        , _timeout_frames(cfg.timeout_frames) {}
    ~dll_object() { unload(); }
    dll_object(const dll_object &) = delete;
    dll_object &operator=(const dll_object &) = delete;

    bool load(const char *path) noexcept {
        lock_writer();
        unload_internal_locked();
        module_handle m = detail::load_module(path);
        if (!m) {
            ABIX_LOG_ERROR("dll_object::load: failed to load module '%s'", path);
            last_error() = call_error::load_failed;
            unlock_writer();
            return false;
        }
        auto getter = reinterpret_cast<const table *(*)(void)>(detail::resolve_symbol(m, SKL_ABIX_TABLE_GETTER));
        if (!getter) {
            ABIX_LOG_ERROR("dll_object::load: abi_get_table not found in '%s'", path);
            detail::unload_module(m);
            last_error() = call_error::load_failed;
            unlock_writer();
            return false;
        }
        const table *t = getter();
        if (!t || t->magic != SKL_ABIX_TABLE_MAGIC || t->format_version != SKL_ABIX_TABLE_FORMAT_VERSION) {
            ABIX_LOG_ERROR("dll_object::load: bad table magic/version in '%s'", path);
            detail::unload_module(m);
            last_error() = call_error::load_failed;
            unlock_writer();
            return false;
        }
        dll_image *img = new (std::nothrow) dll_image{m, t};
        if (!img) {
            ABIX_LOG_ERROR("dll_object::load: failed to allocate image");
            detail::unload_module(m);
            last_error() = call_error::load_failed;
            unlock_writer();
            return false;
        }
        atomic::store_release((void **)&_image, img);
        atomic::store_release(&_state, (uint32_t)image_state::active);
        _refs = 0;
        last_error() = call_error::none;
        ABIX_LOG_INFO("dll_object::load: loaded '%s' (%u entries)", path, t->count);
        unlock_writer();
        return true;
    }

    bool unload() noexcept {
        if (_refs > 0) {
            ABIX_LOG_WARNING("dll_object::unload: %u live handles, cannot unload", _refs);
            last_error() = call_error::stale_handle;
            return false;
        }
        lock_writer();
        bool ok = unload_internal_locked();
        unlock_writer();
        return ok;
    }

    void force_unload() noexcept {
        ABIX_LOG_WARNING("dll_object::force_unload: bypassing RCU, caller must ensure safety");
        lock_writer();
        dll_image *img = static_cast<dll_image *>(atomic::exchange_acq_rel((void **)&_image, nullptr));
        atomic::store_release(&_state, (uint32_t)image_state::zombie);
        if (img) {
            detail::unload_module(img->module);
            delete img;
            _refs = 0;
        }
        unlock_writer();
    }

    bool reload(const char *path) noexcept {
        ABIX_LOG_INFO("dll_object::reload: loading new module, then atomic swap and retire old");

        module_handle new_m = detail::load_module(path);
        if (!new_m) {
            ABIX_LOG_ERROR("dll_object::reload: failed to load module '%s'", path);
            last_error() = call_error::load_failed;
            return false;
        }
        auto getter = reinterpret_cast<const table *(*)(void)>(detail::resolve_symbol(new_m, SKL_ABIX_TABLE_GETTER));
        if (!getter) {
            ABIX_LOG_ERROR("dll_object::reload: abi_get_table not found in '%s'", path);
            detail::unload_module(new_m);
            last_error() = call_error::load_failed;
            return false;
        }
        const table *new_t = getter();
        if (!new_t || new_t->magic != SKL_ABIX_TABLE_MAGIC || new_t->format_version != SKL_ABIX_TABLE_FORMAT_VERSION) {
            ABIX_LOG_ERROR("dll_object::reload: bad table magic/version in '%s'", path);
            detail::unload_module(new_m);
            last_error() = call_error::load_failed;
            return false;
        }

        dll_image *new_img = new (std::nothrow) dll_image{new_m, new_t};
        if (!new_img) {
            ABIX_LOG_ERROR("dll_object::reload: failed to allocate image");
            detail::unload_module(new_m);
            last_error() = call_error::load_failed;
            return false;
        }

        lock_writer();
        dll_image *old_img = static_cast<dll_image *>(atomic::exchange_acq_rel((void **)&_image, new_img));
        atomic::store_release(&_state, (uint32_t)image_state::active);
        _refs = 0;
        unlock_writer();

        if (old_img) {
            rcu_domain::instance().retire(old_img, detail::reclaim_image);
            rcu_domain::instance().synchronize();
        }

        last_error() = call_error::none;
        ABIX_LOG_INFO("dll_object::reload: reloaded '%s' (%u entries)", path, new_t->count);
        return true;
    }

    bool is_loaded() const noexcept {
        return atomic::load_acquire(&_state) == (uint32_t)image_state::active
            && atomic::load_acquire((void * const *)&_image) != nullptr;
    }
    const table *get_table() const noexcept {
        dll_image *img = static_cast<dll_image *>(atomic::load_acquire((void * const *)&_image));
        return img ? img->table : nullptr;
    }
    module_handle module() const noexcept {
        dll_image *img = static_cast<dll_image *>(atomic::load_acquire((void * const *)&_image));
        return img ? img->module : nullptr;
    }

    dll_image *image_acquire() const noexcept {
        return static_cast<dll_image *>(atomic::load_acquire((void * const *)&_image));
    }

    uint32_t ref_count() const noexcept { return _refs; }
    void add_ref() noexcept { atomic::inc_relaxed(&_refs); }
    void release_ref() noexcept {
        if (_refs > 0) atomic::dec_relaxed(&_refs);
    }

    const table *enter_read() noexcept {
        rcu_domain::instance().enter();
        dll_image *img = static_cast<dll_image *>(atomic::load_acquire((void * const *)&_image));
        if (!img) {
            rcu_domain::instance().exit();
            last_error() = call_error::unloading;
            return nullptr;
        }
        return img->table;
    }

    void exit_read() noexcept { rcu_domain::instance().exit(); }

    void set_timeout_policy(RCUTimeoutPolicy policy) noexcept { _timeout_policy = policy; }
    RCUTimeoutPolicy timeout_policy() const noexcept { return _timeout_policy; }

private:
    void lock_writer() noexcept {
        while (!atomic::cas_relaxed(&_writer_lock, 0, 1)) {}
    }
    void unlock_writer() noexcept { atomic::store_relaxed(&_writer_lock, 0); }

    bool unload_internal_locked() noexcept {
        dll_image *img = static_cast<dll_image *>(atomic::exchange_acq_rel((void **)&_image, nullptr));
        atomic::store_release(&_state, (uint32_t)image_state::zombie);
        if (img) {
            rcu_domain::instance().retire(img, detail::reclaim_image);
            rcu_domain::instance().synchronize();
            _refs = 0;
            ABIX_LOG_INFO("dll_object::unload: unloaded");
        }
        last_error() = call_error::none;
        return true;
    }

    dll_image *_image = nullptr;
    uint32_t _refs = 0;
    uint32_t _state = (uint32_t)image_state::zombie;
    uint32_t _writer_lock = 0;
    uint64_t _timeout_ms = ABIX_RCU_TIMEOUT_MS;
    uint64_t _timeout_frames = ABIX_RCU_TIMEOUT_FRAMES_DEFAULT;
    RCUTimeoutPolicy _timeout_policy = RCUTimeoutPolicy::Safe;
};

SKL_ABIX_NAMESPACE_END
#endif