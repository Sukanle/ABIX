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
#ifndef SKL_ABIX_RUNTIME_TYPE_SIG_H
#define SKL_ABIX_RUNTIME_TYPE_SIG_H

#include "mics/utils/hash.h"
#include "mics/utils/type_hash.h"

#include "ABIX/Runtime/Type.h"

namespace skl::abix {

#define SKL_ABIX_TYPE_TAG(T, tag) STATIC_TYPE_TAG(T, tag)
namespace dll {
template<typename T>
class UniquePtr;
template<typename T>
class RefPtr;
template<typename T>
class ViewPtr;
template<typename T>
class SharedPtr;
template<typename T>
class WeakPtr;
// template<typename Sig, cc::tag C = SKL_ABIX_CCPICK(Cdecl)>
// class Function;

}   // namespace dll

namespace runtime {

template<typename Sig>
class Function;

namespace detail {

template<typename T>
struct TypeSigImpl;

template<typename T>
struct TypeSigImpl<dll::UniquePtr<T>> {
    static constexpr sig_t value =
        mics::utils::mix(mics::utils::cstr64("skl::abix::dll::UniquePtr"), mics::utils::type_hash<T>());
};
template<typename T>
struct TypeSigImpl<dll::RefPtr<T>> {
    static constexpr sig_t value =
        mics::utils::mix(mics::utils::cstr64("skl::abix::dll::RefPtr"), mics::utils::type_hash<T>());
};
template<typename T>
struct TypeSigImpl<dll::ViewPtr<T>> {
    static constexpr sig_t value =
        mics::utils::mix(mics::utils::cstr64("skl::abix::dll::ViewPtr"), mics::utils::type_hash<T>());
};
template<typename T>
struct TypeSigImpl<dll::SharedPtr<T>> {
    static constexpr sig_t value =
        mics::utils::mix(mics::utils::cstr64("skl::abix::dll::SharedPtr"), mics::utils::type_hash<T>());
};
template<typename T>
struct TypeSigImpl<dll::WeakPtr<T>> {
    static constexpr sig_t value =
        mics::utils::mix(mics::utils::cstr64("skl::abix::dll::WeakPtr"), mics::utils::type_hash<T>());
};

template<typename R, typename... Args>
struct TypeSigImpl<runtime::Function<R(Args...)>> {
    static constexpr sig_t value = [] {
        sig_t h = mics::utils::cstr64("skl::abix::runtime::Function");
        h = mics::utils::mix(h, mics::utils::type_hash<R>());
        sig_t acc = 0;
        ((acc = mics::utils::mix(acc, mics::utils::type_hash<std::remove_cv_t<Args>>())), ...);
        return mics::utils::mix(h, acc);
    }();
};

template<typename T>
struct TypeSigImpl {
    static constexpr sig_t value = mics::utils::type_hash<T>();
};

}   // namespace detail

}   // namespace runtime

}   // namespace skl::abix

#endif
