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
#ifndef SKL_ABIX_STD_CALLBACK_H
#define SKL_ABIX_STD_CALLBACK_H

// ABIX-STD callback (P1): a function entry point plus an opaque context. The
// lifetime, unregister and thread constraints are part of the caller's
// contract; ABIX only describes the layout.
#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t (*abix_callback_fn)(void *context);

typedef struct abix_callback {
    abix_callback_fn invoke;
    void *context;
} abix_callback;

#ifdef __cplusplus
}
#endif

#endif
