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
#ifndef SKL_ABIX_FUNCTION_H
#define SKL_ABIX_FUNCTION_H
#include <utility>

#include "abix/config.h"     // IWYU pragma: keep
#include "abix/register.h"   // IWYU pragma: keep
#include "abix/obj_dll.h"    // IWYU pragma: keep

SKL_ABIX_NAMESPACE_BEGIN

namespace detail {
constexpr uint64_t SKL_ABIX_CLOSURE_MAGIC = 0XF2A0B1C2D3E4F5A6ULL;

template<typename R, typename... Args>
struct closure_base {
    using invoke_t = R (*)(closure_base *, Args...);
    using destroy_t = void (*)(closure_base *);
    using clone_t = closure_base *(*)(const closure_base *);

    invoke_t invoke;
    destroy_t destroy;
    clone_t clone;
    uint64_t magic;
};

template<typename F, typename R, typename... Args>
struct closure_impl : closure_base<R, Args...> {
    F fn;

    static R invoke_impl(closure_base<R, Args...> *self, Args... a) {
        return static_cast<closure_impl *>(self)->fn(static_cast<Args>(a)...);
    }
    static void destroy_impl(closure_base<R, Args...> *self) noexcept {
        static_cast<closure_impl *>(self)->~closure_impl();
        abi_free(self);
    }
    static closure_base<R, Args...> *clone_impl(const closure_base<R, Args...> *self) {
        const closure_impl *src = static_cast<const closure_impl *>(self);
        void *mem = abi_alloc(sizeof(closure_impl));
        if (!mem) return nullptr;
        return new (mem) closure_impl(*src);
    }

    explicit closure_impl(F &&f)
        : fn(std::move(f)) {
        this->invoke = &invoke_impl;
        this->destroy = &destroy_impl;
        this->clone = &clone_impl;
        this->magic = SKL_ABIX_CLOSURE_MAGIC;
    }
};

}   // namespace detail

template<typename Sig>
class function_dll;

template<typename R, typename... Args>
class function_dll<R(Args...)> {
public:
    static_assert(sizeof(void *) == sizeof(uint64_t), "Only 64-bit platform is supported");

    constexpr function_dll() noexcept
        : _h(0) {}

    template<typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, function_dll>
                                                     && std::is_invocable_r_v<R, F &, Args...>>>
    function_dll(F f) {
        using CF = detail::closure_impl<F, R, Args...>;
        void *mem = abi_alloc(sizeof(CF));
        if (!mem) {
            _h = 0;
            return;
        }
        auto *c = new (mem) CF(std::move(f));
        _h = reinterpret_cast<uint64_t>(c);
    }

    function_dll(const function_dll &o) noexcept
        : _h(0) {
        if (o._h) {
            auto *c = to_closure(o._h);
            auto *clone = c->clone(c);
            _h = clone ? reinterpret_cast<uint64_t>(clone) : 0;
        }
    }
    function_dll &operator=(function_dll rhs) noexcept {
        swap(rhs);
        return *this;
    }
    function_dll(function_dll &&o) noexcept
        : _h(o._h) {
        o._h = 0;
    }
    ~function_dll() noexcept { destroy_closure(); }

    explicit operator bool() const noexcept { return _h != 0; }
    bool empty() const noexcept { return _h == 0; }
    uint64_t handle() const noexcept { return _h; }

    R operator()(Args... a) const {
        if (!_h) {
            last_error() = call_error::invalid;
            return default_ret();
        }
        auto *c = to_closure(_h);
        if (c->magic != detail::SKL_ABIX_CLOSURE_MAGIC) {
            last_error() = call_error::invalid;
            return default_ret();
        }
        last_error() = call_error::none;
        return c->invoke(c, static_cast<Args>(a)...);
    }

    void swap(function_dll &o) noexcept { std::swap(_h, o._h); }
    friend void swap(function_dll &a, function_dll &b) noexcept { a.swap(b); }

private:
    static R default_ret() noexcept {
        if constexpr (std::is_void_v<R>)
            return;
        else
            return R{};
    }
    static detail::closure_base<R, Args...> *to_closure(uint64_t h) noexcept {
        return reinterpret_cast<detail::closure_base<R, Args...> *>(h);
    }
    void destroy_closure() noexcept {
        if (_h) to_closure(_h)->destroy(to_closure(_h));
        _h = 0;
    }

    uint64_t _h = 0;
};

SKL_ABIX_NAMESPACE_END

#endif