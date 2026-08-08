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
#ifndef SKL_ABIX_WEAK_PTR_H
#define SKL_ABIX_WEAK_PTR_H
#include <stdint.h>

#include "abix/config.h"               // IWYU pragma: keep
#include "abix/dll_ptr/shared_ptr.h"   // IWYU pragma: keep

SKL_ABIX_NAMESPACE_BEGIN

template<typename T>
class weak_dll_ptr {
    using control_block = detail::shared_control_block<T>;

public:
    weak_dll_ptr() noexcept = default;

    weak_dll_ptr(const shared_dll_ptr<T> &r) noexcept
        : _cb(r.cb()) {
        if (_cb) ++_cb->weaks;
    }

    weak_dll_ptr(const weak_dll_ptr &o) noexcept
        : _cb(o._cb) {
        if (_cb) ++_cb->weaks;
    }

    weak_dll_ptr(weak_dll_ptr &&o) noexcept
        : _cb(o._cb) {
        o._cb = nullptr;
    }

    weak_dll_ptr &operator=(const shared_dll_ptr<T> &r) noexcept {
        release();
        _cb = r.cb();
        if (_cb) ++_cb->weaks;
        return *this;
    }

    weak_dll_ptr &operator=(const weak_dll_ptr &o) noexcept {
        if (this != &o) {
            release();
            _cb = o._cb;
            if (_cb) ++_cb->weaks;
        }
        return *this;
    }

    weak_dll_ptr &operator=(weak_dll_ptr &&o) noexcept {
        if (this != &o) {
            release();
            _cb = o._cb;
            o._cb = nullptr;
        }
        return *this;
    }

    ~weak_dll_ptr() noexcept { release(); }

    void release() noexcept {
        if (!_cb) return;
        if (--_cb->weaks == 0 && _cb->strongs == 0) {
            abi_free(_cb);
        }
        _cb = nullptr;
    }

    bool alive() const noexcept { return _cb != nullptr && _cb->strongs > 0; }

    bool expired() const noexcept { return !alive(); }

    shared_dll_ptr<T> lock() const noexcept {
        if (!alive()) return shared_dll_ptr<T>();
        return shared_dll_ptr<T>(_cb);
    }

    uint32_t use_count() const noexcept { return _cb ? _cb->strongs : 0U; }

    explicit operator bool() const noexcept { return alive(); }

private:
    control_block *_cb = nullptr;
};

SKL_ABIX_NAMESPACE_END

#endif