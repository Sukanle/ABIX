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
#ifndef SKL_ABIX_REFL_H
#define SKL_ABIX_REFL_H

#include "Reflection/reflect.h"   // IWYU pragma: keep

#include "abix/config.h"   // IWYU pragma: keep
#include "abix/type.h"     // IWYU pragma: keep
#include "abix/fn_dll.h"   // IWYU pragma: keep

SKL_ABIX_NAMESPACE_BEGIN

namespace refl {

template<sig_t SigValue, name_hash_t NameHashValue>
struct fn_entry_tag {
    static constexpr sig_t sig = SigValue;
    static constexpr name_hash_t name_hash = NameHashValue;
};

template<typename TypeList>
struct has_unique_sigs;
template<>
struct has_unique_sigs<SRefl::type_list<>> : std::true_type {};
template<typename T>
struct has_unique_sigs<SRefl::type_list<T>> : std::true_type {};
template<typename T, typename U, typename... Rest>
struct has_unique_sigs<SRefl::type_list<T, U, Rest...>> {
    template<typename V>
    struct same_sig {
        static constexpr bool value = (V::sig == T::sig);
    };
    static constexpr bool value = (SRefl::Fp::count<SRefl::type_list<U, Rest...>, same_sig> == 0)
                               && has_unique_sigs<SRefl::type_list<U, Rest...>>::value;
};

template<typename TypeList, sig_t TargetSig>
struct find_by_sig;
template<sig_t TargetSig>
struct find_by_sig<SRefl::type_list<>, TargetSig> {
    static constexpr int32_t index = -1;
};
template<typename T, typename... Rest, sig_t TargetSig>
struct find_by_sig<SRefl::type_list<T, Rest...>, TargetSig> {
    static constexpr int32_t index = (T::sig == TargetSig)
                                       ? 0
                                       : (find_by_sig<SRefl::type_list<Rest...>, TargetSig>::index == -1
                                                 ? -1
                                                 : 1 + find_by_sig<SRefl::type_list<Rest...>, TargetSig>::index);
};

}   // namespace refl

using DynamicAny = DRefl::Any;

template<typename R, typename... Args>
inline DynamicAny dll_func_call_any(dll_func<R(Args...)> &fn, Args... args) {
    if constexpr (std::is_void_v<R>) {
        fn(static_cast<Args>(args)...);
        return DynamicAny{};
    } else {
        return DynamicAny{fn(static_cast<Args>(args)...)};
    }
}

template<typename T>
inline T any_cast_val(const DynamicAny &a) {
    const T *p = a.try_cast<T>();
    return p ? *p : T{};
}

using DynamicRegistry = DRefl::Registry;

inline void register_dll_table(const table *t, const char *dll_name) {
    if (!t || !dll_name) return;
    auto &reg = DynamicRegistry::instance();
    for (uint32_t i = 0; i < t->count; ++i) {
        const entry &e = t->entries[i];
        if (!e.name) continue;
        static DRefl::FnInfo fn_info;
        fn_info.name = e.name;
        fn_info.is_static = true;
        fn_info.visibility = DRefl::Visibility::Public;
        reg.register_type(nullptr);
        (void)e;
        (void)fn_info;
    }
}

using DynamicTypeInfo = DRefl::TypeInfo;
using DynamicFieldAccessor = DRefl::FieldAccessor;
using DynamicFieldInfo = DRefl::FieldInfo;

template<typename T>
inline DynamicTypeInfo make_pod_type_info(const char *name) {
    static_assert(std::is_trivial_v<T>, "make_pod_type_info requires trivial types");
    DynamicTypeInfo ti;
    ti.name = name;
    ti.type_id = DRefl::type_id_of<T>();
    ti.kind = DRefl::Kind::Struct;
    ti.size = sizeof(T);
    return ti;
}

template<typename T, typename MemberT, size_t Offset>
inline DynamicFieldAccessor make_offset_field(const char *name) {
    DynamicFieldAccessor acc;
    acc.info = DynamicFieldInfo(name, DRefl::type_id_of<T>(), static_cast<uint32_t>(Offset),
        DRefl::FieldKind::MemberVar, DRefl::Visibility::Public);
    acc.getter = [](void *obj) -> void * { return static_cast<char *>(obj) + Offset; };
    acc.setter = [](void *obj, void *value) {
        *reinterpret_cast<MemberT *>(static_cast<char *>(obj) + Offset) = *static_cast<MemberT *>(value);
    };
    return acc;
}

SKL_ABIX_NAMESPACE_END

#endif   // SKL_ABIX_REFL_H
