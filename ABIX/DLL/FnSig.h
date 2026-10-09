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
#ifndef SKL_ABIX_DLL_FNSIG_H
#define SKL_ABIX_DLL_FNSIG_H

#include "ABIX/DLL/Export.h"
#include "ABIX/Runtime/TypeSig.h"
#include "mics/utils/fn_hash.h"

namespace skl::abix {
template<typename T>
constexpr runtime::sig_t type_sig() {
    using U = std::conditional_t<std::is_reference_v<T>, T, std::remove_cv_t<T>>;
    return runtime::detail::TypeSigImpl<U>::value;
}

namespace dll {

template<typename Sig, cc::tag C = SKL_ABIX_CCPICK(Cdecl)>
struct FnSig;
template<cc::tag C, typename Sig>
struct FnSigCC;

namespace detail {
template<cc::tag C, typename Ret, typename... Args>
struct FnSigImpl {
    static constexpr runtime::sig_t value = [] {
        auto h = static_cast<runtime::sig_t>(mics::utils::compute_fn_hash<type_sig<Ret>(), type_sig<Args>()...>());
        h = mics::utils::mix(h, static_cast<runtime::sig_t>(static_cast<uint8_t>(C)) * 0x10001ULL);
        return h;
    }();
};
}   // namespace detail

template<cc::tag C, typename Ret, typename... Args>
struct FnSigCC<C, Ret(Args...)> : detail::FnSigImpl<C, Ret, Args...> {};
template<cc::tag C, typename Ret, typename... Args>
struct FnSigCC<C, Ret (*)(Args...)> : detail::FnSigImpl<C, Ret, Args...> {};

#if defined(_MSC_VER) && !defined(_M_X64)
template<cc::tag C, typename Ret, typename... Args>
struct FnSigCC<C, Ret(__cdecl)(Args...)> : detail::FnSigImpl<C, Ret, Args...> {};
template<cc::tag C, typename Ret, typename... Args>
struct FnSigCC<C, Ret(__cdecl *)(Args...)> : detail::FnSigImpl<C, Ret, Args...> {};
template<cc::tag C, typename Ret, typename... Args>
struct FnSigCC<C, Ret(__stdcall)(Args...)> : detail::FnSigImpl<C, Ret, Args...> {};
template<cc::tag C, typename Ret, typename... Args>
struct FnSigCC<C, Ret(__stdcall *)(Args...)> : detail::FnSigImpl<C, Ret, Args...> {};
#endif

template<typename Sig, cc::tag C>
struct FnSig : FnSigCC<C, Sig> {};

template<typename Sig, cc::tag C = SKL_ABIX_CCPICK(Cdecl)>
inline constexpr auto FnSigV = FnSig<Sig, C>::value;

template<cc::tag C, typename R, typename... Args>
struct FnSigCC<C, runtime::Function<R(Args...)>> : detail::FnSigImpl<C, runtime::Function<R(Args...)>> {};
}   // namespace dll
}   // namespace skl::abix

#endif
