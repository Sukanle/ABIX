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
#ifndef SKL_ABIX_DLL_UniquePtr_H
#define SKL_ABIX_DLL_UniquePtr_H

namespace skl::abix::dll {

template<typename T>
struct UniqueHandle {
    T *ptr;
    void (*destroy)(T *);
};

template<typename T>
class UniquePtr : private UniqueHandle<T> {
    using uhd = UniqueHandle<T>;
    using uhd::destroy;
    using uhd::ptr;

public:
    explicit UniquePtr(T *p = nullptr, void (*d)(T *) = nullptr) noexcept
        : uhd{p, d} {}
    ~UniquePtr() noexcept { reset(); }

    UniquePtr(const UniquePtr &) = delete;
    UniquePtr &operator=(const UniquePtr &) = delete;
    UniquePtr(UniquePtr &&o) noexcept
        : uhd(o.ptr, o.destroy) {
        o.ptr = nullptr;
        o.destroy = nullptr;
    }
    UniquePtr &operator=(UniquePtr &&o) noexcept {
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
struct FnDeleter {
    void (*d)(T *) = nullptr;
    constexpr FnDeleter() noexcept = default;
    constexpr explicit FnDeleter(void (*fn)(T *)) noexcept
        : d(fn) {}
    void operator()(T *p) const noexcept {
        if (p && d) d(p);
    }
};

}   // namespace skl::abix::dll
#endif
