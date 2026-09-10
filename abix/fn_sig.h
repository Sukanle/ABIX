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
#ifndef SKL_ABIX_FN_SIG_H
#define SKL_ABIX_FN_SIG_H

#include "register.h"
#include "type_sig.h"
#include "mics/utils/fn_hash.h"

SKL_ABIX_NAMESPACE_BEGIN
template<typename T>
constexpr sig_t type_sig() {
    using U = std::conditional_t<std::is_reference_v<T>, T, std::remove_cv_t<T>>;
    return detail::type_sig_impl<U>::value;
}

namespace cc {
using tag = mics::utils::cc::tag;
}

template<typename Sig, cc::tag C = SKL_ABIX_CCPICK(Cdecl)>
struct fn_sig;
template<cc::tag C, typename Sig>
struct fn_sig_cc;

namespace detail {
template<cc::tag C, typename Ret, typename... Args>
struct fn_sig_impl {
    static constexpr sig_t value = [] {
        sig_t h = mics::utils::compute_fn_hash<type_sig<Ret>(), type_sig<Args>()...>();
        h = mix(h, static_cast<sig_t>(static_cast<uint8_t>(C)) * 0x10001ULL);
        return h;
    }();
};
}   // namespace detail

template<cc::tag C, typename Ret, typename... Args>
struct fn_sig_cc<C, Ret(Args...)> : detail::fn_sig_impl<C, Ret, Args...> {};
template<cc::tag C, typename Ret, typename... Args>
struct fn_sig_cc<C, Ret (*)(Args...)> : detail::fn_sig_impl<C, Ret, Args...> {};

#if defined(_MSC_VER) && !defined(_M_X64)
template<cc::tag C, typename Ret, typename... Args>
struct fn_sig_cc<C, Ret(__cdecl)(Args...)> : detail::fn_sig_impl<C, Ret, Args...> {};
template<cc::tag C, typename Ret, typename... Args>
struct fn_sig_cc<C, Ret(__cdecl *)(Args...)> : detail::fn_sig_impl<C, Ret, Args...> {};
template<cc::tag C, typename Ret, typename... Args>
struct fn_sig_cc<C, Ret(__stdcall)(Args...)> : detail::fn_sig_impl<C, Ret, Args...> {};
template<cc::tag C, typename Ret, typename... Args>
struct fn_sig_cc<C, Ret(__stdcall *)(Args...)> : detail::fn_sig_impl<C, Ret, Args...> {};
#endif

template<typename Sig, cc::tag C>
struct fn_sig : fn_sig_cc<C, Sig> {};

template<typename Sig, cc::tag C = SKL_ABIX_CCPICK(Cdecl)>
inline constexpr sig_t fn_sig_v = fn_sig<Sig, C>::value;

template<cc::tag C, typename R, typename... Args>
struct fn_sig_cc<C, function_dll<R(Args...)>> : detail::fn_sig_impl<C, function_dll<R(Args...)>> {};

SKL_ABIX_NAMESPACE_END

#endif
