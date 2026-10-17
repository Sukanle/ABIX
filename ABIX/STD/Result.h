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
#ifndef SKL_ABIX_STD_RESULT_H
#define SKL_ABIX_STD_RESULT_H

#include <stdint.h>

// ABIX-STD result (P1): a status plus a value. It reuses the P0 error-boundary
// idea (a status code in the return value) and adds a value payload; the status
// semantics are declared by an [[error]] contract.
#ifdef __cplusplus
namespace abix_std {

template<class T>
struct abix_result {
    int32_t status;
    T value;
};

}   // namespace abix_std

extern "C" {
#endif

typedef abix_std::abix_result<int32_t> abix_result_i32;

#ifdef __cplusplus
}
#endif

#endif
