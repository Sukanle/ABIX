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
#ifndef SKL_ABIX_STD_SPAN_H
#define SKL_ABIX_STD_SPAN_H

#include <stdint.h>

// ABIX-STD: the cross-language standard ABI primitive layer.
//
// ABIX-STD is not a replacement STL. It defines a small set of ABI types and
// contracts that different languages can consume without re-inventing a
// library interface.
//
// The first primitive is a **borrowed span**: a non-owning view of contiguous
// memory, written here as a source-level template. ABIX records the concrete
// instantiations, not the template: the frontend instantiates `abix_span<int32_t>`
// on request (see `[[instantiate]]` in `.abic.toml`) and AMC records the result
// as the concrete entity `abix_span_i32`. Matching it to an ABIX-STD
// `SequenceView<i32>` contract is a separate lowering step; a successful
// instantiation does not by itself claim contract compatibility.
//
// Semantics (see ABI-SPEC.md, "ABIX-STD"):
//
//   borrow  — the view does not own the data and must not release it; it is
//             only valid while the producer keeps the memory alive.
//   owned   — an ABIX-STD owner handle releases through the producer's entry
//             point (see the ownership contract).
//   copy    — independent data produced by an explicit copy.

#include <stdint.h>

namespace abix_std {

template<class T>
struct abix_span {
    const T *data;
    uint64_t len;
};

// A deliberately non-canonical sequence view (length before data). It has the
// same two fields as `abix_span` but a different layout, so it is NOT the
// boundary entity: AMC classifies it as `needs_adapter` and generates a thin
// conversion to the canonical span.
template<class T>
struct abix_view_rev {
    uint64_t len;
    const T *data;
};

}   // namespace abix_std

#ifdef __cplusplus
extern "C" {
#endif

// Concrete ABIX-STD instantiations. The name is the exported ABI entity; it is
// what every target language binds to.
typedef abix_std::abix_span<int32_t> abix_span_i32;
typedef abix_std::abix_view_rev<int32_t> abix_view_rev_i32;

#ifdef __cplusplus
}
#endif

#endif
