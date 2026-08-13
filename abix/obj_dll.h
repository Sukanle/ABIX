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
#include "rcu_timeout.h"
#include "log.h"
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

#if SKL_ABIX_WINDOWS
inline uint32_t atomic_inc_u32(uint32_t *p) noexcept { return (uint32_t)_InterlockedIncrement((volatile long *)p); }
inline uint32_t atomic_dec_u32(uint32_t *p) noexcept { return (uint32_t)_InterlockedDecrement((volatile long *)p); }
inline uint32_t atomic_load_u32(const uint32_t *p) noexcept { return _InterlockedOr((volatile long *)p, 0); }
inline void atomic_store_u32(uint32_t *p, uint32_t v) noexcept { _InterlockedExchange((volatile long *)p, (long)v); }
inline bool atomic_load_bool(const bool *p) noexcept { return *(const volatile bool *)p; }
inline void atomic_store_bool(bool *p, bool v) noexcept { *(volatile bool *)p = v; }
#else
inline uint32_t atomic_inc_u32(uint32_t *p) noexcept { return (uint32_t)__atomic_add_fetch(p, 1, __ATOMIC_ACQ_REL); }
inline uint32_t atomic_dec_u32(uint32_t *p) noexcept { return (uint32_t)__atomic_sub_fetch(p, 1, __ATOMIC_ACQ_REL); }
inline uint32_t atomic_load_u32(const uint32_t *p) noexcept { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
inline void atomic_store_u32(uint32_t *p, uint32_t v) noexcept { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
inline bool atomic_load_bool(const bool *p) noexcept { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
inline void atomic_store_bool(bool *p, bool v) noexcept { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
#endif

enum class rcu_wait_result : uint8_t {
    success = 0,
    timeout = 1
};
}   // namespace detail

class dll_object {
public:
    dll_object() noexcept = default;
    explicit dll_object(const RCUTimeoutConfig &cfg) noexcept
        : _timeout_ms(cfg.timeout_ms)
        , _timeout_frames(cfg.timeout_frames) {}
    ~dll_object() {
        if (detail::atomic_load_bool(&_zombie)) {
            unload_internal(false);
        } else {
            unload();
        }
    }
    dll_object(const dll_object &) = delete;
    dll_object &operator=(const dll_object &) = delete;

    bool load(const char *path) noexcept {
        if (detail::atomic_load_bool(&_zombie)) {
            ABIX_LOG_WARNING("dll_object::load: object is zombie, force-unloading before reload");
            unload_internal(false);
        } else {
            unload();
        }
        module_handle m = detail::load_module(path);
        if (!m) {
            ABIX_LOG_ERROR("dll_object::load: failed to load module '%s'", path);
            last_error() = call_error::load_failed;
            return false;
        }
        auto getter = reinterpret_cast<const table *(*)(void)>(detail::resolve_symbol(m, SKL_ABIX_TABLE_GETTER));
        if (!getter) {
            ABIX_LOG_ERROR("dll_object::load: abi_get_table not found in '%s'", path);
            detail::unload_module(m);
            last_error() = call_error::load_failed;
            return false;
        }
        const table *t = getter();
        if (!t || t->magic != SKL_ABIX_TABLE_MAGIC || t->format_version != SKL_ABIX_TABLE_FORMAT_VERSION) {
            ABIX_LOG_ERROR("dll_object::load: bad table magic/version in '%s'", path);
            detail::unload_module(m);
            last_error() = call_error::load_failed;
            return false;
        }
        _module = m;
        _table = t;
        _refs = 0;
        detail::atomic_store_bool(&_unloading, false);
        detail::atomic_store_bool(&_zombie, false);
        detail::atomic_store_u32(&_active_readers, 0);
        last_error() = call_error::none;
        ABIX_LOG_INFO("dll_object::load: loaded '%s' (%u entries)", path, t->count);
        return true;
    }

    bool unload() noexcept {
        if (_refs > 0) {
            ABIX_LOG_WARNING("dll_object::unload: %u live handles, cannot unload", _refs);
            last_error() = call_error::stale_handle;
            return false;
        }
        return begin_rcu_unload();
    }

    void force_unload() noexcept {
        ABIX_LOG_WARNING("dll_object::force_unload: bypassing RCU, caller must ensure safety");
        unload_internal(false);
    }

    bool reload(const char *path) noexcept {
        ABIX_LOG_INFO("dll_object::reload: force-unloading and reloading");
        unload_internal(false);
        return load(path);
    }

    bool is_loaded() const noexcept {
        return _module != nullptr
            && _table != nullptr
            && !detail::atomic_load_bool(&_unloading)
            && !detail::atomic_load_bool(&_zombie);
    }
    const table *get_table() const noexcept { return _table; }
    module_handle module() const noexcept { return _module; }

    uint32_t ref_count() const noexcept { return _refs; }
    void add_ref() noexcept { ++_refs; }
    void release_ref() noexcept {
        if (_refs > 0) --_refs;
    }

    bool try_enter_read() noexcept {
#if ABIX_LAZY_STARVATION_GUARD >= ABIX_LAZY_STARVATION_GUARD_TICK
        detail::try_passive_check();
#endif
        if (detail::atomic_load_bool(&_unloading) || detail::atomic_load_bool(&_zombie)) {
            last_error() = call_error::unloading;
            return false;
        }
        detail::atomic_inc_u32(&_active_readers);
        if (detail::atomic_load_bool(&_unloading) || detail::atomic_load_bool(&_zombie)) {
            detail::atomic_dec_u32(&_active_readers);
            last_error() = call_error::unloading;
            return false;
        }
        return true;
    }

    void exit_read() noexcept { detail::atomic_dec_u32(&_active_readers); }

    void set_timeout_policy(RCUTimeoutPolicy policy) noexcept { _timeout_policy = policy; }
    RCUTimeoutPolicy timeout_policy() const noexcept { return _timeout_policy; }

    bool begin_rcu_unload() noexcept {
        ABIX_LOG_INFO("dll_object::begin_rcu_unload: marking unloading, waiting for readers");
        detail::atomic_store_bool(&_unloading, true);
        detail::rcu_wait_result result = wait_for_readers();
        if (result == detail::rcu_wait_result::success) {
            ABIX_LOG_INFO("dll_object::begin_rcu_unload: all readers exited, unloading");
            return unload_internal(true);
        }
        return apply_timeout_policy();
    }

private:
    detail::rcu_wait_result wait_for_readers() noexcept {
#if ABIX_RCU_TIMEOUT_ENABLE
        uint64_t start_ms = detail::get_tick_ms();
        uint64_t start_frames = detail::get_tick_frames();
        uint32_t spin_count = 0;
        while (detail::atomic_load_u32(&_active_readers) > 0) {
            ++spin_count;
            if ((spin_count & 0xFFFFF) == 0) {
                uint64_t now_ms = detail::get_tick_ms();
                if (_timeout_ms > 0 && now_ms - start_ms >= _timeout_ms) {
                    ABIX_LOG_ERROR("dll_object::wait_for_readers: ms timeout after %u ms, %u readers remain",
                        (uint32_t)(now_ms - start_ms), detail::atomic_load_u32(&_active_readers));
                    return detail::rcu_wait_result::timeout;
                }
                if (_timeout_frames > 0) {
                    uint64_t now_frames = detail::get_tick_frames();
                    if (now_frames - start_frames >= _timeout_frames) {
                        ABIX_LOG_ERROR("dll_object::wait_for_readers: frame timeout after %u frames, %u readers remain",
                            (uint32_t)(now_frames - start_frames), detail::atomic_load_u32(&_active_readers));
                        return detail::rcu_wait_result::timeout;
                    }
                }
            }
        }
        return detail::rcu_wait_result::success;
#else
        while (detail::atomic_load_u32(&_active_readers) > 0) {}
        return detail::rcu_wait_result::success;
#endif
    }

    bool apply_timeout_policy() noexcept {
        switch (_timeout_policy) {
            case RCUTimeoutPolicy::Safe: {
                ABIX_LOG_ERROR("dll_object: RCU timeout -> Safe: marking zombie, DLL leaked for safety");
                detail::atomic_store_bool(&_zombie, true);
                detail::atomic_store_bool(&_unloading, false);
                last_error() = call_error::unloading;
                return false;
            }
            case RCUTimeoutPolicy::ForceUnload: {
                ABIX_LOG_ERROR("dll_object: RCU timeout -> ForceUnload: forcing unload (may crash active callers)");
                return unload_internal(false);
            }
#if defined(ABIX_ENABLE_FORCE_LEAK_POLICY)
            case RCUTimeoutPolicy::ForceLeak: {
                ABIX_LOG_ERROR("dll_object: RCU timeout -> ForceLeak: detaching DLL, leaking safely");
                _module = nullptr;
                _table = nullptr;
                _refs = 0;
                detail::atomic_store_bool(&_zombie, true);
                detail::atomic_store_bool(&_unloading, false);
                detail::atomic_store_u32(&_active_readers, 0);
                last_error() = call_error::unloading;
                return false;
            }
#endif
            default: {
                detail::atomic_store_bool(&_zombie, true);
                detail::atomic_store_bool(&_unloading, false);
                last_error() = call_error::unloading;
                return false;
            }
        }
    }

    bool unload_internal(bool report) noexcept {
        if (_module) {
            ABIX_LOG_INFO("dll_object::unload_internal: calling unload_module");
            detail::unload_module(_module);
            _module = nullptr;
            _table = nullptr;
            _refs = 0;
            detail::atomic_store_bool(&_unloading, false);
            detail::atomic_store_bool(&_zombie, false);
            detail::atomic_store_u32(&_active_readers, 0);
        }
        if (report) last_error() = call_error::none;
        return true;
    }

    module_handle _module = nullptr;
    const table *_table = nullptr;
    uint32_t _refs = 0;
    bool _unloading = false;
    bool _zombie = false;
    uint32_t _active_readers = 0;
    uint64_t _timeout_ms = ABIX_RCU_TIMEOUT_MS;
    uint64_t _timeout_frames = ABIX_RCU_TIMEOUT_FRAMES_DEFAULT;
    RCUTimeoutPolicy _timeout_policy = RCUTimeoutPolicy::Safe;
};

SKL_ABIX_NAMESPACE_END

#endif