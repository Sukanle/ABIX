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
#ifndef SKL_ABIX_RUNTIME_FUNCTION_H
#define SKL_ABIX_RUNTIME_FUNCTION_H
#include <utility>

#include "ABIX/Util/Config.h"   // IWYU pragma: keep
#include "ABIX/Util/Mem.h"      // IWYU pragma: keep
#include "ABIX/DLL/Export.h"    // IWYU pragma: keep
#include "ABIX/DLL/Object.h"    // IWYU pragma: keep

namespace skl::abix::runtime {

namespace detail {
constexpr uint64_t SKL_ABIX_CLOSURE_MAGIC = 0XF2A0B1C2D3E4F5A6ULL;

template<typename R, typename... Args>
struct ClosureBase {
    using invoke_t = R (*)(ClosureBase *, Args...);
    using destroy_t = void (*)(ClosureBase *);
    using clone_t = ClosureBase *(*)(const ClosureBase *);

    invoke_t invoke;
    destroy_t destroy;
    clone_t clone;
    uint64_t magic;
};

template<typename F, typename R, typename... Args>
struct ClosureImpl : ClosureBase<R, Args...> {
    F fn;

    static R invoke_impl(ClosureBase<R, Args...> *self, Args... a) {
        return static_cast<ClosureImpl *>(self)->fn(a...);
    }
    static void destroy_impl(ClosureBase<R, Args...> *self) noexcept {
        static_cast<ClosureImpl *>(self)->~ClosureImpl();
        skl::abix::mem::dealloc(self);
    }
    static ClosureBase<R, Args...> *clone_impl(const ClosureBase<R, Args...> *self) {
        const auto *src = static_cast<const ClosureImpl *>(self);
        void *mem = skl::abix::mem::alloc(sizeof(ClosureImpl));
        if (!mem) return nullptr;
        return new (mem) ClosureImpl(*src);
    }

    explicit ClosureImpl(F &&f)
        : fn(std::move(f)) {
        this->invoke = &invoke_impl;
        this->destroy = &destroy_impl;
        this->clone = &clone_impl;
        this->magic = SKL_ABIX_CLOSURE_MAGIC;
    }
};

}   // namespace detail

template<typename Sig>
class Function;

template<typename R, typename... Args>
class Function<R(Args...)> {
public:
    static_assert(sizeof(void *) == sizeof(uint64_t), "Only 64-bit platform is supported");

    constexpr Function() noexcept
        : _h(0) {}

    template<typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, Function>
                                                     && std::is_invocable_r_v<R, F &, Args...>>>
    Function(F f) {
        using CF = detail::ClosureImpl<F, R, Args...>;
        void *mem = skl::abix::mem::alloc(sizeof(CF));
        if (!mem) {
            _h = 0;
            return;
        }
        auto *c = new (mem) CF(std::move(f));
        _h = reinterpret_cast<uint64_t>(c);
    }

    Function(const Function &o) noexcept
        : _h(0) {
        if (o._h) {
            auto *c = to_closure(o._h);
            auto *clone = c->clone(c);
            _h = clone ? reinterpret_cast<uint64_t>(clone) : 0;
        }
    }
    Function &operator=(Function rhs) noexcept {
        swap(rhs);
        return *this;
    }
    Function(Function &&o) noexcept
        : _h(o._h) {
        o._h = 0;
    }
    ~Function() noexcept { destroy_closure(); }

    explicit operator bool() const noexcept { return _h != 0; }
    bool empty() const noexcept { return _h == 0; }
    uint64_t handle() const noexcept { return _h; }

    R operator()(Args... a) const {
        if (!_h) {
            dll::last_error() = dll::CallError::invalid;
            return default_ret();
        }
        auto *c = to_closure(_h);
        if (c->magic != detail::SKL_ABIX_CLOSURE_MAGIC) {
            dll::last_error() = dll::CallError::invalid;
            return default_ret();
        }
        dll::last_error() = dll::CallError::none;
        return c->invoke(c, a...);
    }

    void swap(Function &o) noexcept { std::swap(_h, o._h); }
    friend void swap(Function &a, Function &b) noexcept { a.swap(b); }

private:
    static R default_ret() noexcept {
        if constexpr (std::is_void_v<R>)
            return;
        else
            return R{};
    }
    static detail::ClosureBase<R, Args...> *to_closure(uint64_t h) noexcept {
        return reinterpret_cast<detail::ClosureBase<R, Args...> *>(h);
    }
    void destroy_closure() noexcept {
        if (_h) to_closure(_h)->destroy(to_closure(_h));
        _h = 0;
    }

    uint64_t _h = 0;
};

}   // namespace skl::abix::runtime

#endif
