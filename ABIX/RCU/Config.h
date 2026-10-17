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
#ifndef SKL_ABIX_RCU_CONFIG_H
#define SKL_ABIX_RCU_CONFIG_H

#include <stdint.h>

namespace skl::abix::rcu {

// Lifecycle state of a loaded image. `active` means an image is published and
// calls are allowed; `zombie` means nothing is loadable (never loaded, or the
// image has been detached and is on its way to reclamation).
//
// There is deliberately no timeout policy here: the grace period is unbounded, so
// a reader that stays inside a critical section blocks `synchronize()` for as long
// as it stays there. Adding a bounded wait means separating two questions that this
// header must keep apart - "did the grace period finish?" (the domain's answer) and
// "what happens to the old image if it did not?" (the object's decision) - and
// neither should be expressed as a per-object configuration value before the wait
// itself exists.
enum class State : uint32_t {
    active = 0,
    retiring = 1,
    zombie = 2,
};

}   // namespace skl::abix::rcu

#endif   // SKL_ABIX_RCU_CONFIG_H
