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
#ifndef SKL_ABIX_DLL_WEAK_PTR_H
#define SKL_ABIX_DLL_WEAK_PTR_H
#include <stdint.h>

#include "ABIX/Util/Mem.h"
#include "ABIX/DLL/SharedPtr.h"

namespace skl::abix::dll {

template<typename T>
class WeakPtr {
    using ControlBlock = detail::SharedControlBlock<T>;

public:
    WeakPtr() noexcept = default;

    WeakPtr(const SharedPtr<T> &r) noexcept
        : _cb(r.cb()) {
        if (_cb) ++_cb->weaks;
    }

    WeakPtr(const WeakPtr &o) noexcept
        : _cb(o._cb) {
        if (_cb) ++_cb->weaks;
    }

    WeakPtr(WeakPtr &&o) noexcept
        : _cb(o._cb) {
        o._cb = nullptr;
    }

    WeakPtr &operator=(const SharedPtr<T> &r) noexcept {
        release();
        _cb = r.cb();
        if (_cb) ++_cb->weaks;
        return *this;
    }

    WeakPtr &operator=(const WeakPtr &o) noexcept {
        if (this != &o) {
            release();
            _cb = o._cb;
            if (_cb) ++_cb->weaks;
        }
        return *this;
    }

    WeakPtr &operator=(WeakPtr &&o) noexcept {
        if (this != &o) {
            release();
            _cb = o._cb;
            o._cb = nullptr;
        }
        return *this;
    }

    ~WeakPtr() noexcept { release(); }

    void release() noexcept {
        if (!_cb) return;
        if (--_cb->weaks == 0 && _cb->strongs == 0) {
            skl::abix::mem::dealloc(_cb);
        }
        _cb = nullptr;
    }

    bool alive() const noexcept { return _cb != nullptr && _cb->strongs > 0; }

    bool expired() const noexcept { return !alive(); }

    SharedPtr<T> lock() const noexcept {
        if (!alive()) return SharedPtr<T>();
        return SharedPtr<T>(_cb);
    }

    uint32_t use_count() const noexcept { return _cb ? _cb->strongs : 0U; }

    explicit operator bool() const noexcept { return alive(); }

private:
    ControlBlock *_cb = nullptr;
};

}   // namespace skl::abix::dll

#endif
