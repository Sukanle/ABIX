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
#ifndef SKL_ABIX_VIEW_PTR_H
#define SKL_ABIX_VIEW_PTR_H
#include <stdint.h>

#include "abix/config.h"            // IWYU pragma: keep
#include "abix/dll_ptr/ref_ptr.h"   // IWYU pragma: keep

SKL_ABIX_NAMESPACE_BEGIN

template<typename T>
class view_dll_ptr {
    using control_block = detail::ref_control_block<T>;

public:
    view_dll_ptr() noexcept = default;

    view_dll_ptr(const ref_dll_ptr<T> &r) noexcept
        : _cb(r.cb()) {
        if (_cb) ++_cb->views;
    }

    view_dll_ptr(const view_dll_ptr &o) noexcept
        : _cb(o._cb) {
        if (_cb) ++_cb->views;
    }

    view_dll_ptr(view_dll_ptr &&o) noexcept
        : _cb(o._cb) {
        o._cb = nullptr;
    }

    view_dll_ptr &operator=(const ref_dll_ptr<T> &r) noexcept {
        release();
        _cb = r.cb();
        if (_cb) ++_cb->views;
        return *this;
    }

    view_dll_ptr &operator=(const view_dll_ptr &o) noexcept {
        if (this != &o) {
            release();
            _cb = o._cb;
            if (_cb) ++_cb->views;
        }
        return *this;
    }

    view_dll_ptr &operator=(view_dll_ptr &&o) noexcept {
        if (this != &o) {
            release();
            _cb = o._cb;
            o._cb = nullptr;
        }
        return *this;
    }

    ~view_dll_ptr() noexcept { release(); }

    void release() noexcept {
        if (!_cb) return;
        if (--_cb->views == 0 && _cb->refs == 0) {
            abi_free(_cb);
        }
        _cb = nullptr;
    }

    bool alive() const noexcept { return _cb != nullptr && _cb->refs > 0; }

    bool expired() const noexcept { return !alive(); }

    ref_dll_ptr<T> lock() const noexcept {
        if (!alive()) return ref_dll_ptr<T>();
        return ref_dll_ptr<T>(_cb);
    }

    uint32_t use_count() const noexcept { return _cb ? _cb->refs : 0U; }

    explicit operator bool() const noexcept { return alive(); }

private:
    control_block *_cb = nullptr;
};

SKL_ABIX_NAMESPACE_END

#endif