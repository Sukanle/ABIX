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

#ifndef SKL_ABIX_SHARED_PTR_H
#define SKL_ABIX_SHARED_PTR_H

#include <atomic>
#include <new>   // IWYU pragma: keep

#include "abix/config.h"               // IWYU pragma: keep
#include "abix/register.h"             // IWYU pragma: keep
#include "abix/dll_ptr/unique_ptr.h"   // IWYU pragma: keep

SKL_ABIX_NAMESPACE_BEGIN

template<typename T>
class weak_dll_ptr;

namespace detail {

template<typename T>
struct shared_control_block {
    T *ptr;
    void (*destroy)(T *);
    std::atomic<uint32_t> strongs;
    std::atomic<uint32_t> weaks;
    uint64_t magic;   // alignment
};

}   // namespace detail

template<typename T>
class shared_dll_ptr {
    friend class weak_dll_ptr<T>;
    using control_block = detail::shared_control_block<T>;

    explicit shared_dll_ptr(control_block *cb) noexcept
        : _cb(cb) {
        if (_cb) ++_cb->strongs;
    }

public:
    explicit shared_dll_ptr(T *p = nullptr, void (*d)(T *) = nullptr) noexcept
        : _cb(nullptr) {
        if (!p) return;
        void *mem = abi_alloc(sizeof(control_block));
        if (!mem) return;
        _cb = new (mem) control_block{p, d, 1ULL, 0ULL, SKL_ABIX_MAGIC64};
    }
    shared_dll_ptr(const shared_dll_ptr &o) noexcept
        : _cb(o._cb) {
        if (_cb) ++_cb->strongs;
    }
    shared_dll_ptr(shared_dll_ptr &&o) noexcept
        : _cb(o._cb) {
        o._cb = nullptr;
    }
    shared_dll_ptr &operator=(const shared_dll_ptr &o) noexcept {
        if (this != &o) {
            release();
            _cb = o._cb;
            if (_cb) ++_cb->strongs;
        }
        return *this;
    }
    shared_dll_ptr &operator=(shared_dll_ptr &&o) noexcept {
        if (this != &o) {
            release();
            _cb = o._cb;
            o._cb = nullptr;
        }
        return *this;
    }
    ~shared_dll_ptr() noexcept {
        if (!_cb) return;
        if (--_cb->strongs == 0) {
            if (_cb->destroy) _cb->destroy(_cb->ptr);
            _cb->ptr = nullptr;
            if (_cb->weaks == 0) abi_free(_cb);
        }
        _cb = nullptr;
    }

    unique_handle<T> *release() noexcept {
        if (!_cb) return nullptr;
        if (_cb->strongs == 1) {
            unique_handle<T> p(_cb->ptr, _cb->destroy);
            _cb = nullptr;
            return p;
        }
        return nullptr;
    }

    T *get() const noexcept { return _cb ? _cb->ptr : nullptr; }
    T *operator->() const noexcept { return get(); }
    T &operator*() const noexcept { return *get(); }

    uint32_t use_count() const noexcept { return _cb ? _cb->strongs : 0U; }
    uint32_t weak_count() const noexcept { return _cb ? _cb->weaks : 0U; }

    bool try_unique() noexcept { return use_count() == 1; }

    template<typename D>
    static shared_dll_ptr<D> from_unique(unique_handle<D> uhd) noexcept {
        return shared_dll_ptr<D>(uhd.ptr, uhd.destroy);
    }

    explicit operator bool() const noexcept { return _cb != nullptr && _cb->ptr != nullptr; }

    control_block *cb() const noexcept { return _cb; }

private:
    control_block *_cb = nullptr;
};

SKL_ABIX_NAMESPACE_END

#endif