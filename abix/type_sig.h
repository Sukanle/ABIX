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
#ifndef SKL_ABIX_TYPE_SIG_H
#define SKL_ABIX_TYPE_SIG_H

#include <type_traits>

#include "mics/utils/hash.h"
#include "mics/utils/type_hash.h"

#include "abix/type.h"     // IWYU pragma: keep
#include "abix/config.h"   // IWYU pragma: keep

SKL_ABIX_NAMESPACE_BEGIN

#define SKL_ABIX_TYPE_TAG(T, tag) STATIC_TYPE_TAG(T, tag)

template<typename T>
class unique_dll_ptr;
template<typename T>
class ref_dll_ptr;
template<typename T>
class view_dll_ptr;
template<typename T>
class shared_dll_ptr;
template<typename T>
class weak_dll_ptr;
template<typename Sig>
class function_dll;

namespace detail {
using mics::utils::cstr64;
using mics::utils::mix;
using mics::utils::type_hash;

template<typename T>
struct type_sig_impl;
template<typename T>
struct type_sig_impl<unique_dll_ptr<T>> {
    static constexpr sig_t value = mix(cstr64("abix::unique_dll_ptr"), type_hash<T>());
};
template<typename T>
struct type_sig_impl<ref_dll_ptr<T>> {
    static constexpr sig_t value = mix(cstr64("abix::ref_dll_ptr"), type_hash<T>());
};
template<typename T>
struct type_sig_impl<view_dll_ptr<T>> {
    static constexpr sig_t value = mix(cstr64("abix::view_dll_ptr"), type_hash<T>());
};
template<typename T>
struct type_sig_impl<shared_dll_ptr<T>> {
    static constexpr sig_t value = mix(cstr64("abix::shared_dll_ptr"), type_hash<T>());
};
template<typename T>
struct type_sig_impl<weak_dll_ptr<T>> {
    static constexpr sig_t value = mix(cstr64("abix::weak_dll_ptr"), type_hash<T>());
};
template<typename R, typename... Args>
struct type_sig_impl<function_dll<R(Args...)>> {
    static constexpr sig_t value = [] {
        sig_t h = cstr64("skl::abix::function_dll");
        h = mix(h, type_hash<R>());
        sig_t acc = 0;
        ((acc = mix(acc, type_hash<std::remove_cv_t<Args>>())), ...);
        h = mix(h, acc);
        return h;
    }();
};

template<typename T>
struct type_sig_impl {
    static constexpr sig_t value = type_hash<T>();
};
}   // namespace detail

SKL_ABIX_NAMESPACE_END

#endif
