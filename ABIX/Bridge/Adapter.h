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
#ifndef SKL_ABIX_BRIDGE_ADAPTER_H
#define SKL_ABIX_BRIDGE_ADAPTER_H

// Typed ABI adapter dispatch.
//
// `amc adapter --typed` generates a specialization of `abix::adapter<Source,
// Target>` for each compatible type pair; the specialization applies the ABIX
// field mapping from raw source memory to raw target memory. The primary
// template reports "no adapter" so callers can fall back gracefully.
namespace skl::abix::bridge {

template<typename Source, typename Target>
struct Adapter {
    static bool apply(void *target, const void *source) noexcept {
        (void)target;
        (void)source;
        return false;
    }
};

}   // namespace skl::abix::bridge

#endif   // SKL_ABIX_ADAPTER_H
