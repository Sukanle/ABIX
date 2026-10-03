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
#ifndef SKL_ABIX_DLL_REFPTR_H
#define SKL_ABIX_DLL_REFPTR_H

#include <stdint.h>

#include <new>   // IWYU pragma: keep

#include "ABIX/Util/Config.h"
#include "ABIX/Util/Mem.h"
#include "ABIX/DLL/Export.h"
#include "ABIX/DLL/UniquePtr.h"

namespace skl::abix::dll {

template<typename T>
class view_ptr;

namespace detail {

template<typename T>
struct RefControlBlock {
    T *ptr;
    void (*destroy)(T *);
    uint32_t refs;
    uint32_t views;
    uint64_t magic;   // alignment
};
}   // namespace detail

template<typename T>
class RefPtr {
    friend class view_ptr<T>;
    using ControlBlock = detail::RefControlBlock<T>;

    explicit RefPtr(ControlBlock *cb) noexcept
        : _cb(cb) {
        if (_cb) ++_cb->refs;
    }

public:
    explicit RefPtr(T *p = nullptr, void (*d)(T *) = nullptr) noexcept
        : _cb(nullptr) {
        if (!p) return;
        void *mem = skl::abix::mem::alloc(sizeof(ControlBlock));
        if (!mem) return;
        _cb = new (mem) ControlBlock{p, d, 1U, 0U, SKL_ABIX_MAGIC64};
    }
    RefPtr(const RefPtr &o) noexcept
        : _cb(o._cb) {
        if (_cb) ++_cb->refs;
    }
    RefPtr(RefPtr &&o) noexcept
        : _cb(o._cb) {
        o._cb = nullptr;
    }
    RefPtr &operator=(const RefPtr &o) noexcept {
        if (this != &o) {
            reset();
            _cb = o._cb;
            if (_cb) ++_cb->refs;
        }
        return *this;
    }
    RefPtr &operator=(RefPtr &&o) noexcept {
        if (this != &o) {
            reset();
            _cb = o._cb;
            o._cb = nullptr;
        }
        return *this;
    }
    ~RefPtr() noexcept { reset(); }

    UniqueHandle<T> release() noexcept {
        if (!_cb || _cb->refs != 1) return UniqueHandle<T>{nullptr, nullptr};
        UniqueHandle<T> handle{_cb->ptr, _cb->destroy};
        _cb->ptr = nullptr;
        _cb->destroy = nullptr;
        reset();
        return handle;
    }

    T *get() const noexcept { return _cb ? _cb->ptr : nullptr; }
    T *operator->() const noexcept { return get(); }
    T &operator*() const noexcept { return *get(); }

    uint32_t use_count() const noexcept { return _cb ? _cb->refs : 0U; }
    uint32_t view_count() const noexcept { return _cb ? _cb->views : 0U; }
    bool try_unique() noexcept { return use_count() == 1; }

    template<typename D>
    static RefPtr<D> from_unique(UniqueHandle<D> uhd) noexcept {
        return RefPtr<D>(uhd.ptr, uhd.destroy);
    }

    explicit operator bool() const noexcept { return _cb != nullptr && _cb->ptr != nullptr; }

    ControlBlock *cb() const noexcept { return _cb; }

private:
    void reset() noexcept {
        if (!_cb) return;
        if (--_cb->refs == 0) {
            if (_cb->destroy) _cb->destroy(_cb->ptr);
            _cb->ptr = nullptr;
            if (_cb->views == 0) skl::abix::mem::dealloc(_cb);
        }
        _cb = nullptr;
    }

    ControlBlock *_cb = nullptr;
};

}   // namespace skl::abix::dll

#endif
