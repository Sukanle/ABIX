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

#ifndef SKL_ABIX_DLL_SHARED_PTR_H
#define SKL_ABIX_DLL_SHARED_PTR_H

#include <atomic>
#include <new>   // IWYU pragma: keep

#include "ABIX/Util/Config.h"
#include "ABIX/Util/Mem.h"
#include "ABIX/DLL/Export.h"
#include "ABIX/DLL/UniquePtr.h"

namespace skl::abix::dll {

template<typename T>
class WeakPtr;

namespace detail {

template<typename T>
struct SharedControlBlock {
    T *ptr;
    void (*destroy)(T *);
    std::atomic<uint32_t> strongs;
    std::atomic<uint32_t> weaks;
    uint64_t magic;   // alignment
};

}   // namespace detail

template<typename T>
class SharedPtr {
    friend class WeakPtr<T>;
    using ControlBlock = detail::SharedControlBlock<T>;

    explicit SharedPtr(ControlBlock *cb) noexcept
        : _cb(cb) {
        if (_cb) ++_cb->strongs;
    }

public:
    explicit SharedPtr(T *p = nullptr, void (*d)(T *) = nullptr) noexcept
        : _cb(nullptr) {
        if (!p) return;
        void *mem = skl::abix::mem::alloc(sizeof(ControlBlock));
        if (!mem) return;
        _cb = new (mem) ControlBlock{p, d, 1ULL, 0ULL, SKL_ABIX_MAGIC64};
    }
    SharedPtr(const SharedPtr &o) noexcept
        : _cb(o._cb) {
        if (_cb) ++_cb->strongs;
    }
    SharedPtr(SharedPtr &&o) noexcept
        : _cb(o._cb) {
        o._cb = nullptr;
    }
    SharedPtr &operator=(const SharedPtr &o) noexcept {
        if (this != &o) {
            reset();
            _cb = o._cb;
            if (_cb) ++_cb->strongs;
        }
        return *this;
    }
    SharedPtr &operator=(SharedPtr &&o) noexcept {
        if (this != &o) {
            reset();
            _cb = o._cb;
            o._cb = nullptr;
        }
        return *this;
    }
    ~SharedPtr() noexcept { reset(); }

    UniqueHandle<T> release() noexcept {
        if (!_cb || _cb->strongs.load() != 1) return UniqueHandle<T>{nullptr, nullptr};
        UniqueHandle<T> handle{_cb->ptr, _cb->destroy};
        _cb->ptr = nullptr;
        _cb->destroy = nullptr;
        reset();
        return handle;
    }

    T *get() const noexcept { return _cb ? _cb->ptr : nullptr; }
    T *operator->() const noexcept { return get(); }
    T &operator*() const noexcept { return *get(); }

    uint32_t use_count() const noexcept { return _cb ? _cb->strongs.load() : 0U; }
    uint32_t weak_count() const noexcept { return _cb ? _cb->weaks.load() : 0U; }

    bool try_unique() noexcept { return use_count() == 1; }

    template<typename D>
    static SharedPtr<D> from_unique(UniqueHandle<D> uhd) noexcept {
        return SharedPtr<D>(uhd.ptr, uhd.destroy);
    }

    explicit operator bool() const noexcept { return _cb != nullptr && _cb->ptr != nullptr; }

    ControlBlock *cb() const noexcept { return _cb; }

private:
    void reset() noexcept {
        if (!_cb) return;
        if (--_cb->strongs == 0) {
            if (_cb->destroy) _cb->destroy(_cb->ptr);
            _cb->ptr = nullptr;
            if (_cb->weaks.load() == 0) skl::abix::mem::dealloc(_cb);
        }
        _cb = nullptr;
    }

    ControlBlock *_cb = nullptr;
};

}   // namespace skl::abix::dll

#endif
