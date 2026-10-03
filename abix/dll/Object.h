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
#ifndef SKL_ABIX_DLL_OBJECT_H
#define SKL_ABIX_DLL_OBJECT_H
#include "ABIX/Runtime/Type.h"
#include "ABIX/DLL/Export.h"
#include "ABIX/DLL/SharedPtr.h"
#include "ABIX/Runtime/Search.h"
#include "ABIX/RCU/Config.h"
#include "ABIX/RCU/Domain.h"
#include "ABIX/Util/Atomic.h"
#include "ABIX/Util/Log.h"
#include <new>
#ifdef SKL_ABIX_WINDOWS
#  include <libloaderapi.h>
#endif

namespace skl::abix::dll {

#ifdef SKL_ABIX_WINDOWS
using ModuleHandle = HMODULE;
#else
#  include <dlfcn.h>
using ModuleHandle = void *;
#endif

enum class CallError : uint8_t {
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
inline CallError &last_error() noexcept {
    static CallError e = CallError::none;
    return e;
}

class Object;

struct Image {
    ModuleHandle module;
    const runtime::Table *export_table;
    runtime::HashIndex index;
};

namespace detail {
inline ModuleHandle load_module(const char *path) noexcept {
#ifdef SKL_ABIX_WINDOWS
    return LoadLibraryA(path);
#else
    ModuleHandle m = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!m) {
        const char *err = dlerror();
        ABIX_LOG_ERROR("dlopen('%s') failed: %s", path, err ? err : "unknown error");
    }
    return m;
#endif
}
inline void *resolve_symbol(ModuleHandle m, const char *name) noexcept {
    if (!m) return nullptr;
#ifdef SKL_ABIX_WINDOWS
    return reinterpret_cast<void *>(GetProcAddress(m, name));
#else
    return dlsym(m, name);
#endif
}
inline void unload_module(ModuleHandle m) noexcept {
    if (m) {
#ifdef SKL_ABIX_WINDOWS
        FreeLibrary(m);
#else
        dlclose(m);
#endif
    }
}

inline void reclaim_image(void *p) noexcept {
    auto *img = static_cast<Image *>(p);
    img->index.destroy();
    unload_module(img->module);
    delete img;
}
}   // namespace detail

// Shared, reference-counted state for one loaded module. The `Object` facade
// and every live `Function` handle hold a strong reference. Unloading acts on
// `image`; the Control itself outlives all handles, so a handle destructor is
// always safe even after the `Object` has been destroyed.
struct Control {
    Image *image = nullptr;
    uint32_t state = static_cast<uint32_t>(rcu::State::zombie);
    uint32_t writer_lock = 0;
    uint64_t timeout_ms = ABIX_RCU_TIMEOUT_MS;
    uint64_t timeout_frames = ABIX_RCU_TIMEOUT_FRAMES_DEFAULT;
    rcu::TimeoutPolicy timeout_policy = rcu::TimeoutPolicy::Safe;
};

namespace detail {
inline void destroy_control(Control *c) noexcept {
    if (!c) return;
    if (auto *img = c->image) {
        rcu::Domain::instance().retire(img, reclaim_image);
        rcu::Domain::instance().synchronize();
    }
    delete c;
}
}   // namespace detail

class Object {
public:
    Object() noexcept
        : _c(make_control()) {}
    explicit Object(const rcu::TimeoutConfig &cfg) noexcept
        : _c(make_control()) {
        if (_c) {
            _c->timeout_ms = cfg.timeout_ms;
            _c->timeout_frames = cfg.timeout_frames;
        }
    }
    ~Object() { unload(); }
    Object(const Object &) = delete;
    Object &operator=(const Object &) = delete;

    SharedPtr<Control> control() const noexcept { return _c; }

    bool load(const char *path) noexcept {
        if (!_c) {
            last_error() = CallError::load_failed;
            return false;
        }
        lock_writer();
        unload_internal_locked();
        ModuleHandle m = detail::load_module(path);
        if (!m) {
            ABIX_LOG_ERROR("Object::load: failed to load module '%s'", path);
            last_error() = CallError::load_failed;
            unlock_writer();
            return false;
        }
        auto getter =
            reinterpret_cast<const runtime::Table *(*)(void)>(detail::resolve_symbol(m, SKL_ABIX_TABLE_GETTER));
        if (!getter) {
            ABIX_LOG_ERROR("Object::load: abi_get_table not found in '%s'", path);
            detail::unload_module(m);
            last_error() = CallError::load_failed;
            unlock_writer();
            return false;
        }
        const auto *t = getter();
        if (!t || t->magic != SKL_ABIX_TABLE_MAGIC || t->format_version != SKL_ABIX_TABLE_FORMAT_VERSION) {
            ABIX_LOG_ERROR("Object::load: bad table magic/version in '%s'", path);
            detail::unload_module(m);
            last_error() = CallError::load_failed;
            unlock_writer();
            return false;
        }
        Image *img = new (std::nothrow) Image{m, t, {}};
        if (!img) {
            ABIX_LOG_ERROR("Object::load: failed to allocate image");
            detail::unload_module(m);
            last_error() = CallError::load_failed;
            unlock_writer();
            return false;
        }
        if (t->count >= SKL_ABIX_HASH_THRESHOLD) img->index.build(*t);
        util::store_release(&_c->image, img);
        util::store_release(&_c->state, static_cast<uint32_t>(rcu::State::active));
        last_error() = CallError::none;
        ABIX_LOG_INFO("Object::load: loaded '%s' (%u entries)", path, t->count);
        unlock_writer();
        return true;
    }

    bool unload() noexcept {
        if (!_c) return false;
        if (_c.use_count() > 1) {
            ABIX_LOG_WARNING("Object::unload: %u live handles, cannot unload", _c.use_count() - 1);
            last_error() = CallError::stale_handle;
            return false;
        }
        lock_writer();
        bool ok = unload_internal_locked();
        unlock_writer();
        return ok;
    }

    void force_unload() noexcept {
        if (!_c) return;
        ABIX_LOG_WARNING("Object::force_unload: bypassing RCU, caller must ensure safety");
        lock_writer();
        auto *img = util::exchange_acq_rel(&_c->image, nullptr);
        util::store_release(&_c->state, static_cast<uint32_t>(rcu::State::zombie));
        if (img) {
            img->index.destroy();
            detail::unload_module(img->module);
            delete img;
        }
        unlock_writer();
    }

    bool reload(const char *path) noexcept {
        if (!_c) {
            last_error() = CallError::load_failed;
            return false;
        }
        ABIX_LOG_INFO("Object::reload: loading new module, then util swap and retire old");

        ModuleHandle new_m = detail::load_module(path);
        if (!new_m) {
            ABIX_LOG_ERROR("Object::reload: failed to load module '%s'", path);
            last_error() = CallError::load_failed;
            return false;
        }
        auto getter =
            reinterpret_cast<const runtime::Table *(*)()>(detail::resolve_symbol(new_m, SKL_ABIX_TABLE_GETTER));
        if (!getter) {
            ABIX_LOG_ERROR("Object::reload: abi_get_table not found in '%s'", path);
            detail::unload_module(new_m);
            last_error() = CallError::load_failed;
            return false;
        }
        const auto *new_t = getter();
        if (!new_t || new_t->magic != SKL_ABIX_TABLE_MAGIC || new_t->format_version != SKL_ABIX_TABLE_FORMAT_VERSION) {
            ABIX_LOG_ERROR("Object::reload: bad table magic/version in '%s'", path);
            detail::unload_module(new_m);
            last_error() = CallError::load_failed;
            return false;
        }

        auto *new_img = new (std::nothrow) Image{new_m, new_t, {}};
        if (!new_img) {
            ABIX_LOG_ERROR("Object::reload: failed to allocate image");
            detail::unload_module(new_m);
            last_error() = CallError::load_failed;
            return false;
        }
        if (new_t->count >= SKL_ABIX_HASH_THRESHOLD) new_img->index.build(*new_t);

        lock_writer();
        auto *old_img = util::exchange_acq_rel(&_c->image, new_img);
        util::store_release(&_c->state, static_cast<uint32_t>(rcu::State::active));
        unlock_writer();

        if (old_img) {
            rcu::Domain::instance().retire(old_img, detail::reclaim_image);
            rcu::Domain::instance().synchronize();
        }

        last_error() = CallError::none;
        ABIX_LOG_INFO("Object::reload: reloaded '%s' (%u entries)", path, new_t->count);
        return true;
    }

    bool is_loaded() const noexcept {
        return _c && util::load_acquire(&_c->state) == static_cast<uint32_t>(rcu::State::active)
            && util::load_acquire(&_c->image) != nullptr;
    }
    const runtime::Table *get_table() const noexcept {
        if (!_c) return nullptr;
        auto *img = util::load_acquire(&_c->image);
        return img ? img->export_table : nullptr;
    }
    ModuleHandle module() const noexcept {
        if (!_c) return nullptr;
        auto *img = util::load_acquire(&_c->image);
        return img ? img->module : nullptr;
    }

    Image *image_acquire() const noexcept { return _c ? util::load_acquire(&_c->image) : nullptr; }

    uint32_t ref_count() const noexcept { return _c.use_count() > 0 ? _c.use_count() - 1 : 0; }

    const runtime::Table *enter_read() noexcept {
        rcu::Domain::instance().enter();
        auto *img = _c ? util::load_acquire(&_c->image) : nullptr;
        if (!img) {
            rcu::Domain::instance().exit();
            last_error() = CallError::unloading;
            return nullptr;
        }
        return img->export_table;
    }

    void exit_read() noexcept { rcu::Domain::instance().exit(); }

    void set_timeout_policy(rcu::TimeoutPolicy policy) noexcept {
        if (_c) _c->timeout_policy = policy;
    }
    rcu::TimeoutPolicy timeout_policy() const noexcept {
        return _c ? _c->timeout_policy : rcu::TimeoutPolicy::Safe;
    }

private:
    static SharedPtr<Control> make_control() noexcept {
        auto *c = new (std::nothrow) Control{};
        if (!c) return SharedPtr<Control>{};
        return SharedPtr<Control>(c, detail::destroy_control);
    }

    void lock_writer() noexcept {
        if (_c) {
            while (!util::cas_relaxed(&_c->writer_lock, 0U, 1U)) {}
        }
    }
    void unlock_writer() noexcept {
        if (_c) util::store_relaxed(&_c->writer_lock, 0U);
    }

    bool unload_internal_locked() noexcept {
        auto *img = util::exchange_acq_rel(&_c->image, nullptr);
        util::store_release(&_c->state, static_cast<uint32_t>(rcu::State::zombie));
        if (img) {
            rcu::Domain::instance().retire(img, detail::reclaim_image);
            rcu::Domain::instance().synchronize();
            ABIX_LOG_INFO("Object::unload: unloaded");
        }
        last_error() = CallError::none;
        return true;
    }

    SharedPtr<Control> _c;
};

}   // namespace skl::abix::dll
#endif
