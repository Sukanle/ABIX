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
#include <string>
#ifdef SKL_ABIX_WINDOWS
#  include <libloaderapi.h>
#  include <winnt.h>
#else
#  include <dlfcn.h>
#endif

namespace skl::abix::dll {

#ifdef SKL_ABIX_WINDOWS
using ModuleHandle = HMODULE;
#else
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
    // A configured reclaim timeout also bounds how long an operation waits for the
    // Control writer lock, so a host with a time budget is not left spinning behind
    // another operation. Only reported when a timeout is configured.
    lock_timeout = 10,
};

// Stable spelling for diagnostics and logs. Keep this in sync with the enum;
// the numeric values above are a host-side compatibility surface and must not
// be renumbered.
constexpr const char *to_string(CallError error) noexcept {
    switch (error) {
        case CallError::none:             return "none";
        case CallError::not_loaded:       return "not_loaded";
        case CallError::not_found:        return "not_found";
        case CallError::sig_mismatch:     return "sig_mismatch";
        case CallError::version_mismatch: return "version_mismatch";
        case CallError::stale_handle:     return "stale_handle";
        case CallError::table_changed:    return "table_changed";
        case CallError::invalid:          return "invalid";
        case CallError::load_failed:      return "load_failed";
        case CallError::unloading:        return "unloading";
        case CallError::lock_timeout:     return "lock_timeout";
    }
    return "unknown";
}

static_assert(static_cast<uint8_t>(CallError::none) == 0, "CallError values are a stable host-side surface");
static_assert(static_cast<uint8_t>(CallError::lock_timeout) == 10, "CallError values are a stable host-side surface");

// Per-thread, per-DSO host diagnostic state. This is deliberately not a
// cross-module error channel: callers must not read another module's
// last_error(), and an inline header definition can produce one copy per DSO.
inline CallError &last_error() noexcept {
    static thread_local CallError e = CallError::none;
    return e;
}

// What happened to the image retired by the most recent load/reload/unload. Kept
// separate from the operation's own success: swapping in a new module can succeed
// while the previous one is still on its way out.
enum class ReclaimOutcome : uint8_t {
    not_attempted = 0,      // this operation retired nothing
    reclaimed = 1,          // the retired image was released
    deferred_batched = 2,   // no timeout configured, and the batched schedule had not
                            // advanced yet: it is released by a later operation
    deferred_timeout = 3,   // a reclaim timeout was configured and the grace period was
                            // not proven in time; the image stays retired and mapped
};

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

// The index slot array is retired separately from the image it belongs to, so the
// buffer can be handed back to the pool one reload later while the module itself
// stays mapped until the batched reclaim. That split is what makes a reload reuse
// its predecessor's buffer (the index is the expensive part to rebuild) without
// paying a fresh module map on every swap (`Domain::retire_eager`).
inline void reclaim_index(void *p) noexcept { static_cast<runtime::HashIndex *>(p)->destroy(); }
}   // namespace detail

// Shared, reference-counted state for one loaded module. The `Object` facade
// and every live `Function` handle hold a strong reference. Unloading acts on
// `image`; the Control itself outlives all handles, so a handle destructor is
// always safe even after the `Object` has been destroyed.
struct Control {
    Image *image = nullptr;
    uint32_t state = static_cast<uint32_t>(rcu::State::zombie);
    uint32_t writer_lock = 0;
    // Reclaim policy. The *duration* is configuration; every operation derives a
    // fresh deadline from it, so a configured timeout is never reused as an absolute
    // instant. 0 means unbounded, which is the default and keeps the batched
    // behaviour described in docs/abix/api.md.
    uint64_t reclaim_timeout_ms = 0;
    uint32_t last_reclaim = static_cast<uint32_t>(ReclaimOutcome::not_attempted);
};

namespace detail {
// Hands a swapped-out image over the way the two RCU classes expect: the index
// slot array goes to the eager class (reusable by the very next build), the image
// itself stays in the batched class (its module keeps serving loads that still find
// it resident). Called with the writer lock held; the synchronize calls take the
// domain's own lock, which is a leaf.
//
// When `c` asks for a bounded reclaim, the wait for the image's grace period is
// bounded by a deadline derived from `c->reclaim_timeout_ms` - fresh for every call -
// and the outcome is recorded on the Control. A timeout changes nothing else: the
// image stays retired, mapped, and is released by a later successful grace period
// (the "safe" policy; there is deliberately no automatic forced unload).
inline void retire_image(Control *c, Image *img) noexcept {
    auto &dom = rcu::Domain::instance();
    const bool bounded = c != nullptr && c->reclaim_timeout_ms != 0;

    dom.retire_eager(&img->index, reclaim_index);

    bool proven = false;
    uint64_t token = 0;
    if (bounded) {
        // One bounded wait covers both classes, so the index buffer is recycled by the
        // same grace period rather than by a separate unbounded eager sync (which would
        // let a stuck reader block the reload before the deadline was ever consulted).
        token = dom.retire_token(img, reclaim_image);
        proven = dom.synchronize_until(rcu::Deadline::after_ms(c->reclaim_timeout_ms));
    } else {
        dom.synchronize_eager();   // unbounded, unchanged
        token = dom.retire_token(img, reclaim_image);
        dom.synchronize();   // unchanged batched behaviour
    }

    if (c) {
        // The watermark is the honest answer to "was this image released?", which the
        // outcome reports without having to observe the callback itself (the callback
        // runs on whichever thread collects, and the Image is gone afterwards).
        const bool done = dom.reclaimed_through(token, rcu::ReclaimClass::batched);
        ReclaimOutcome outcome = ReclaimOutcome::deferred_batched;
        if (done) {
            outcome = ReclaimOutcome::reclaimed;
        } else if (bounded && !proven) {
            outcome = ReclaimOutcome::deferred_timeout;
        }
        util::store_release(&c->last_reclaim, static_cast<uint32_t>(outcome));
    }
}

inline void destroy_control(Control *c) noexcept {
    if (!c) return;
    if (auto *img = c->image) retire_image(c, img);
    delete c;
}
}   // namespace detail

class Object {
public:
    Object() noexcept
        : _c(make_control()) {}
    // The destructor must complete, so it waits for the writer lock unconditionally
    // (unload() itself honours a configured reclaim timeout).
    ~Object() { unload_impl(/*bounded=*/false); }
    Object(const Object &) = delete;
    Object &operator=(const Object &) = delete;

    SharedPtr<Control> control() const noexcept { return _c; }

    bool load(const char *path) noexcept {
        if (!_c) {
            last_error() = CallError::load_failed;
            return false;
        }
        // The new module is loaded and indexed with no lock held, and the writer lock
        // is taken only for the swap (publish_image). Holding it across LoadLibrary +
        // the index build was the long critical section that made every other
        // operation's lock acquisition unbounded. A failed load now leaves the
        // previous image in place, which is what reload() already did.
        Image *new_img = load_image(path);
        if (!new_img) {
            ABIX_LOG_ERROR("Object::load: failed to load module '%s'", path);
            last_error() = CallError::load_failed;
            return false;
        }
        // Read the entry count before publishing: once published, another operation
        // may retire the image and free it.
        const uint32_t entries = new_img->export_table ? new_img->export_table->count : 0u;
        if (!publish_image(new_img, rcu::State::active)) {
            discard_image(new_img);
            last_error() = CallError::lock_timeout;
            return false;
        }
        last_error() = CallError::none;
        _path = path;
        ABIX_LOG_INFO("Object::load: loaded '%s' (%u entries)", path, entries);
        return true;
    }

    bool unload() noexcept { return unload_impl(/*bounded=*/true); }

    void force_unload() noexcept {
        if (!_c) return;
        ABIX_LOG_WARNING("Object::force_unload: bypassing RCU, caller must ensure safety");
        // Deliberately unbounded: this is the path that must complete. The lock is
        // held only for the swap; the image is freed outside it.
        if (!lock_writer(/*bounded=*/false)) return;
        auto *img = util::exchange_acq_rel(&_c->image, nullptr);
        util::store_release(&_c->state, static_cast<uint32_t>(rcu::State::zombie));
        unlock_writer();
        if (img) {
            img->index.destroy();
            detail::unload_module(img->module);
            delete img;
        }
    }

    bool reload(const char *path) noexcept {
        if (!_c) {
            last_error() = CallError::load_failed;
            return false;
        }
        ABIX_LOG_INFO("Object::reload: loading new module, then swap and retire old");

        Image *new_img = load_image(path);
        if (!new_img) {
            ABIX_LOG_ERROR("Object::reload: failed to load module '%s'", path);
            last_error() = CallError::load_failed;
            return false;
        }
        // Read the entry count before publishing: once published, another operation
        // may retire the image and free it.
        const uint32_t entries = new_img->export_table ? new_img->export_table->count : 0u;
        if (!publish_image(new_img, rcu::State::active)) {
            discard_image(new_img);
            last_error() = CallError::lock_timeout;
            return false;
        }
        last_error() = CallError::none;
        _path = path;
        ABIX_LOG_INFO("Object::reload: reloaded '%s' (%u entries)", path, entries);
        return true;
    }

    bool is_loaded() const noexcept {
        return _c
            && util::load_acquire(&_c->state) == static_cast<uint32_t>(rcu::State::active)
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

    // The load path (owned by the Object; stable while it is alive). Used to
    // take a second OS-level reference on POSIX, where a dlopen handle alone
    // cannot re-open the object.
    const char *module_path() const noexcept { return _path.empty() ? nullptr : _path.c_str(); }

    // Resolve a raw exported symbol from the loaded module. This is the fixed
    // entry point path (for example the ABIX Interface discovery entry); it does
    // not go through the name+sig table, and it takes no protection: the symbol is
    // valid only for as long as the module stays loaded. `unload()` is refused
    // while a handle is alive, but `reload()` swaps the image whenever it is
    // asked to, so re-resolve after a reload instead of caching the result.
    void *resolve(const char *name) const noexcept { return detail::resolve_symbol(module(), name); }

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

    // Bounds how long a load/reload/unload waits for a retired image's grace period.
    // The value is a *duration*: every operation derives a fresh deadline from it, so
    // a configured timeout is never reused as an absolute instant. 0 (the default)
    // means unbounded, which keeps the batched behaviour - see the reclaim notes in
    // docs/abix/api.md. Enabling a bound is an explicit trade: the wait then asks for
    // a real grace period on every operation instead of sharing the batched one.
    void set_reclaim_timeout_ms(uint64_t ms) noexcept {
        if (_c) _c->reclaim_timeout_ms = ms;
    }
    uint64_t reclaim_timeout_ms() const noexcept { return _c ? _c->reclaim_timeout_ms : 0; }

    // What happened to the image retired by the most recent operation. Never reports
    // a failure of the operation itself: a reload that swapped the module in is still
    // successful when the previous image is deferred.
    ReclaimOutcome reclaim_outcome() const noexcept {
        return _c ? static_cast<ReclaimOutcome>(util::load_acquire(&_c->last_reclaim)) : ReclaimOutcome::not_attempted;
    }

private:
    static SharedPtr<Control> make_control() noexcept {
        auto *c = new (std::nothrow) Control{};
        if (!c) return SharedPtr<Control>{};
        return SharedPtr<Control>(c, detail::destroy_control);
    }

    // Takes the Control writer lock. `bounded == false` (the destructor and
    // force_unload: paths that must complete) always waits; otherwise a configured
    // reclaim timeout bounds the wait, because a host with a time budget should not
    // spin behind another operation. Returns false on timeout, in which case the
    // caller has changed nothing.
    bool lock_writer(bool bounded) noexcept {
        if (!_c) return false;
        if (!bounded || _c->reclaim_timeout_ms == 0) {
            while (!util::cas_relaxed(&_c->writer_lock, 0U, 1U)) {}
            return true;
        }
        const rcu::Deadline deadline = rcu::Deadline::after_ms(_c->reclaim_timeout_ms);
        unsigned polls = 0;
        while (!util::cas_relaxed(&_c->writer_lock, 0U, 1U)) {
            // Poll the clock rarely: a spin is cheap, an expired() read is not.
            if ((++polls & 0xFFu) == 0 && deadline.expired()) return false;
        }
        return true;
    }
    void unlock_writer() noexcept {
        if (_c) util::store_relaxed(&_c->writer_lock, 0U);
    }

    // Swaps `new_img` (nullptr for unload) into the object under the writer lock,
    // then retires the previous image **outside** the lock. Holding the lock only
    // for the swap is what keeps another operation's lock acquisition bounded:
    // LoadLibrary, the index build and the grace-period wait all happen elsewhere.
    // On a lock timeout nothing changes; `retired_out` (when given) receives the
    // previous image so the caller can tell whether anything was retired.
    bool publish_image(Image *new_img, rcu::State state, Image **retired_out = nullptr) noexcept {
        if (!lock_writer(/*bounded=*/true)) return false;
        Image *previous = util::exchange_acq_rel(&_c->image, new_img);
        util::store_release(&_c->state, static_cast<uint32_t>(state));
        unlock_writer();
        if (retired_out) *retired_out = previous;
        if (previous) detail::retire_image(_c.get(), previous);
        return true;
    }

    // Loads and indexes a module without touching the writer lock. Returns nullptr
    // on any failure; nothing is published in that case.
    Image *load_image(const char *path) noexcept {
        ModuleHandle m = detail::load_module(path);
        if (!m) return nullptr;
        auto getter =
            reinterpret_cast<const runtime::Table *(*)(void)>(detail::resolve_symbol(m, SKL_ABIX_TABLE_GETTER));
        const runtime::Table *t = getter ? getter() : nullptr;
        if (t && (t->magic != SKL_ABIX_TABLE_MAGIC || t->format_version != SKL_ABIX_TABLE_FORMAT_VERSION)) {
            detail::unload_module(m);
            return nullptr;
        }
        auto *img = new (std::nothrow) Image{m, t, {}};
        if (!img) {
            detail::unload_module(m);
            return nullptr;
        }
        if (t && t->count >= SKL_ABIX_HASH_THRESHOLD) img->index.build(*t);
        return img;
    }

    // Frees an image that was never published, so no reader can observe it: no
    // grace period is needed.
    static void discard_image(Image *img) noexcept {
        if (!img) return;
        img->index.destroy();
        detail::unload_module(img->module);
        delete img;
    }

    // Detaches the image under the writer lock and retires it outside. `bounded`
    // selects whether a configured reclaim timeout also bounds the lock wait; the
    // destructor passes false because it must complete.
    bool unload_impl(bool bounded) noexcept {
        if (!_c) return false;
        if (_c.use_count() > 1) {
            ABIX_LOG_WARNING("Object::unload: %u live handles, cannot unload", _c.use_count() - 1);
            last_error() = CallError::stale_handle;
            return false;
        }
        if (!lock_writer(bounded)) {
            last_error() = CallError::lock_timeout;
            return false;
        }
        Image *old = util::exchange_acq_rel(&_c->image, nullptr);
        util::store_release(&_c->state, static_cast<uint32_t>(rcu::State::zombie));
        unlock_writer();
        if (old) {
            detail::retire_image(_c.get(), old);
            ABIX_LOG_INFO("Object::unload: unloaded");
        }
        last_error() = CallError::none;
        return true;
    }

    SharedPtr<Control> _c;
    std::string _path;
};

}   // namespace skl::abix::dll
#endif
