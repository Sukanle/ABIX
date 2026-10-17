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
#ifndef SKL_ABIX_STD_OPTIONAL_H
#define SKL_ABIX_STD_OPTIONAL_H

#include <stdint.h>

// ABIX-STD optional (P1): a value that may be absent. The discriminant is an
// explicit field, not a language-native optional layout.
#ifdef __cplusplus
namespace abix_std {

template<class T>
struct abix_optional {
    uint8_t has_value;
    T value;
};

}   // namespace abix_std

extern "C" {
#endif

typedef abix_std::abix_optional<int32_t> abix_optional_i32;

#ifdef __cplusplus
}
#endif

#endif
