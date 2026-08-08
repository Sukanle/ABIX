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
#include "abix/abix.hpp"   // IWYU pragma: keep

using Cb = skl::abix::function_dll<void(int)>;

namespace {
Cb s_cb;
int s_has_cb = 0;
int s_last_invoked = -1;
}   // namespace

void register_callback(Cb cb) {
    s_cb = std::move(cb);
    s_has_cb = s_cb ? 1 : 0;
}
extern "C" int has_callback() { return s_has_cb; }
extern "C" int invoke_callback(int v) {
    if (!s_has_cb || s_cb.empty()) return -1;
    s_cb(v);
    s_last_invoked = v;
    return 0;
}
extern "C" int get_last_invoked() { return s_last_invoked; }
extern "C" void clear_callback() {
    s_cb = Cb();
    s_has_cb = 0;
}

// clang-format off
SKL_ABIX_DEFINE_TABLE(
    SKL_ABIX_ENTRY("clear_callback", clear_callback),
    SKL_ABIX_ENTRY("get_last_invoked", get_last_invoked),
    SKL_ABIX_ENTRY("has_callback", has_callback),
    SKL_ABIX_ENTRY("invoke_callback", invoke_callback),
    SKL_ABIX_ENTRY("register_callback", register_callback),
)
// clang-format on
