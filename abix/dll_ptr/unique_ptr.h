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
#ifndef SKL_ABIX_UNIQUE_PTR_H
#define SKL_ABIX_UNIQUE_PTR_H

#include "abix/config.h"   // IWYU pragma: keep

SKL_ABIX_NAMESPACE_BEGIN

template<typename T>
struct unique_handle {
    T *ptr;
    void (*destroy)(T *);
};

template<typename T>
class unique_dll_ptr : private unique_handle<T> {
    using uhd = unique_handle<T>;
    using uhd::destroy;
    using uhd::ptr;

public:
    explicit unique_dll_ptr(T *p = nullptr, void (*d)(T *) = nullptr) noexcept
        : uhd{p, d} {}
    ~unique_dll_ptr() noexcept { reset(); }

    unique_dll_ptr(const unique_dll_ptr &) = delete;
    unique_dll_ptr &operator=(const unique_dll_ptr &) = delete;
    unique_dll_ptr(unique_dll_ptr &&o) noexcept
        : uhd(o.ptr, o.destroy) {
        o.ptr = nullptr;
        o.destroy = nullptr;
    }
    unique_dll_ptr &operator=(unique_dll_ptr &&o) noexcept {
        if (this != &o) {
            reset();
            ptr = o.ptr;
            destroy = o.destroy;
            o.ptr = nullptr;
            o.destroy = nullptr;
        }
        return *this;
    }

    void reset(T *p = nullptr, void (*d)(T *) = nullptr) noexcept {
        if (ptr && destroy) destroy(ptr);
        ptr = p;
        destroy = d;
    }
    uhd *release() noexcept {
        uhd *p = ptr;
        ptr = nullptr;
        destroy = nullptr;
        return p;
    }
    T *get() const noexcept { return ptr; }
    T *operator->() const noexcept { return ptr; }
    T &operator*() const noexcept { return *ptr; }
    explicit operator bool() const noexcept { return ptr != nullptr; }
};

template<typename T>
struct fn_deleter {
    void (*d)(T *) = nullptr;
    constexpr fn_deleter() noexcept = default;
    constexpr fn_deleter(void (*fn)(T *)) noexcept
        : d(fn) {}
    void operator()(T *p) const noexcept {
        if (p && d) d(p);
    }
};

SKL_ABIX_NAMESPACE_END
#endif
