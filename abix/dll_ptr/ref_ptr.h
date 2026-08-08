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
#ifndef SKL_ABIX_REF_PTR_H
#define SKL_ABIX_REF_PTR_H

#include <stdint.h>

#include <new>   // IWYU pragma: keep

#include "abix/config.h"               // IWYU pragma: keep
#include "abix/register.h"             // IWYU pragma: keep
#include "abix/dll_ptr/unique_ptr.h"   // IWYU pragma: keep

SKL_ABIX_NAMESPACE_BEGIN

template<typename T>
class view_dll_ptr;

namespace detail {

template<typename T>
struct alignas(8) ref_control_block {
    T *ptr;
    void (*destroy)(T *);
    uint32_t refs;
    uint32_t views;
    uint64_t magic;   // alignment
};
}   // namespace detail

template<typename T>
class ref_dll_ptr {
    friend class view_dll_ptr<T>;
    using control_block = detail::ref_control_block<T>;

    explicit ref_dll_ptr(control_block *cb) noexcept
        : _cb(cb) {
        if (_cb) ++_cb->refs;
    }

public:
    explicit ref_dll_ptr(T *p = nullptr, void (*d)(T *) = nullptr) noexcept
        : _cb(nullptr) {
        if (!p) return;
        void *mem = abi_alloc(sizeof(control_block));
        if (!mem) return;
        _cb = new (mem) control_block{p, d, 1U, 0U, SKL_ABIX_MAGIC64};
    }
    ref_dll_ptr(const ref_dll_ptr &o) noexcept
        : _cb(o._cb) {
        if (_cb) ++_cb->refs;
    }
    ref_dll_ptr(ref_dll_ptr &&o) noexcept
        : _cb(o._cb) {
        o._cb = nullptr;
    }
    ref_dll_ptr &operator=(const ref_dll_ptr &o) noexcept {
        if (this != &o) {
            release();
            _cb = o._cb;
            if (_cb) ++_cb->refs;
        }
        return *this;
    }
    ref_dll_ptr &operator=(ref_dll_ptr &&o) noexcept {
        if (this != &o) {
            release();
            _cb = o._cb;
            o._cb = nullptr;
        }
        return *this;
    }
    ~ref_dll_ptr() noexcept {
        if (!_cb) return;
        if (--_cb->refs == 0) {
            _cb->destroy(_cb->ptr);
            _cb->ptr = nullptr;
            if (_cb->views == 0) abi_free(_cb);
        }
        _cb = nullptr;
    }

    unique_handle<T> *release() noexcept {
        if (!_cb) return nullptr;
        if (_cb->refs == 1) {
            unique_handle<T> p(_cb->ptr, _cb->destroy);
            _cb = nullptr;
            return p;
        }
        return nullptr;
    }

    T *get() const noexcept { return _cb ? _cb->ptr : nullptr; }
    T *operator->() const noexcept { return get(); }
    T &operator*() const noexcept { return *get(); }

    uint32_t use_count() const noexcept { return _cb ? _cb->refs : 0U; }
    uint32_t view_count() const noexcept { return _cb ? _cb->views : 0U; }
    bool try_unique() noexcept { return use_count() == 1; }

    template<typename D>
    static ref_dll_ptr<D> from_unique(unique_handle<D> uhd) noexcept {
        return ref_dll_ptr<D>(uhd.ptr, uhd.destroy);
    }

    explicit operator bool() const noexcept { return _cb != nullptr && _cb->ptr != nullptr; }

    control_block *cb() const noexcept { return _cb; }

private:
    control_block *_cb = nullptr;
};

SKL_ABIX_NAMESPACE_END

#endif