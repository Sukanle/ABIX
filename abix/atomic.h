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
#ifndef SKL_ABIX_ATOMIC_H
#define SKL_ABIX_ATOMIC_H

#include "config.h"
#if SKL_ABIX_WINDOWS
#  include <intrin.h>
#endif

SKL_ABIX_NAMESPACE_BEGIN

namespace atomic {

#if SKL_ABIX_WINDOWS

// --- u32 load/store (volatile access, not RMW) ---
// x86/x64 TSO already provides acquire/release hardware ordering;
// volatile + compiler barrier suffices for both /volatile:ms and /volatile:iso.
inline uint32_t load_acquire(const uint32_t *p) noexcept {
    uint32_t v = *(volatile const uint32_t *)p;
    _ReadWriteBarrier();
    return v;
}
inline uint32_t load_relaxed(const uint32_t *p) noexcept { return *(volatile const uint32_t *)p; }
inline void store_release(uint32_t *p, uint32_t v) noexcept {
    _ReadWriteBarrier();
    *(volatile uint32_t *)p = v;
}
inline void store_relaxed(uint32_t *p, uint32_t v) noexcept { *(volatile uint32_t *)p = v; }

// --- u32 RMW (keep Interlocked) ---
inline uint32_t inc_relaxed(uint32_t *p) noexcept { return (uint32_t)_InterlockedIncrement((volatile long *)p); }
inline uint32_t dec_relaxed(uint32_t *p) noexcept { return (uint32_t)_InterlockedDecrement((volatile long *)p); }
inline uint32_t exchange_acq_rel(uint32_t *p, uint32_t v) noexcept {
    return (uint32_t)_InterlockedExchange((volatile long *)p, (long)v);
}
inline bool cas_relaxed(uint32_t *p, uint32_t expected, uint32_t desired) noexcept {
    return (uint32_t)_InterlockedCompareExchange((volatile long *)p, (long)desired, (long)expected) == expected;
}

// --- u64 load/store ---
inline uint64_t load_acquire(const uint64_t *p) noexcept {
    uint64_t v = *(volatile const uint64_t *)p;
    _ReadWriteBarrier();
    return v;
}
inline uint64_t load_relaxed(const uint64_t *p) noexcept { return *(volatile const uint64_t *)p; }
inline void store_release(uint64_t *p, uint64_t v) noexcept {
    _ReadWriteBarrier();
    *(volatile uint64_t *)p = v;
}
inline void store_relaxed(uint64_t *p, uint64_t v) noexcept { *(volatile uint64_t *)p = v; }

// --- u64 RMW ---
inline uint64_t inc_acq_rel(uint64_t *p) noexcept { return (uint64_t)_InterlockedIncrement64((volatile long long *)p); }
inline uint64_t exchange_acq_rel(uint64_t *p, uint64_t v) noexcept {
    return (uint64_t)_InterlockedExchange64((volatile long long *)p, (long long)v);
}

// --- pointer load/store ---
inline void *load_acquire(void * const *p) noexcept {
    void *v = *(void * const volatile *)p;
    _ReadWriteBarrier();
    return v;
}
inline void store_release(void **p, void *v) noexcept {
    _ReadWriteBarrier();
    *(void * volatile *)p = v;
}
inline void *exchange_acq_rel(void **p, void *v) noexcept { return _InterlockedExchangePointer(p, v); }

#else   // GCC / Clang

// --- u32 ---
inline uint32_t load_acquire(const uint32_t *p) noexcept { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
inline uint32_t load_relaxed(const uint32_t *p) noexcept { return __atomic_load_n(p, __ATOMIC_RELAXED); }
inline void store_release(uint32_t *p, uint32_t v) noexcept { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
inline void store_relaxed(uint32_t *p, uint32_t v) noexcept { __atomic_store_n(p, v, __ATOMIC_RELAXED); }
inline uint32_t inc_relaxed(uint32_t *p) noexcept { return __atomic_add_fetch(p, 1, __ATOMIC_RELAXED); }
inline uint32_t dec_relaxed(uint32_t *p) noexcept { return __atomic_sub_fetch(p, 1, __ATOMIC_RELAXED); }
inline uint32_t exchange_acq_rel(uint32_t *p, uint32_t v) noexcept {
    return __atomic_exchange_n(p, v, __ATOMIC_ACQ_REL);
}
inline bool cas_relaxed(uint32_t *p, uint32_t expected, uint32_t desired) noexcept {
    return __atomic_compare_exchange_n(p, &expected, desired, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
}

// --- u64 ---
inline uint64_t load_acquire(const uint64_t *p) noexcept { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
inline uint64_t load_relaxed(const uint64_t *p) noexcept { return __atomic_load_n(p, __ATOMIC_RELAXED); }
inline void store_release(uint64_t *p, uint64_t v) noexcept { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
inline void store_relaxed(uint64_t *p, uint64_t v) noexcept { __atomic_store_n(p, v, __ATOMIC_RELAXED); }
inline uint64_t inc_acq_rel(uint64_t *p) noexcept { return __atomic_add_fetch(p, 1, __ATOMIC_ACQ_REL); }
inline uint64_t exchange_acq_rel(uint64_t *p, uint64_t v) noexcept {
    return __atomic_exchange_n(p, v, __ATOMIC_ACQ_REL);
}

// --- pointer ---
inline void *load_acquire(void * const *p) noexcept { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
inline void store_release(void **p, void *v) noexcept { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
inline void *exchange_acq_rel(void **p, void *v) noexcept { return __atomic_exchange_n(p, v, __ATOMIC_ACQ_REL); }

#endif   // SKL_ABIX_WINDOWS

}   // namespace atomic

SKL_ABIX_NAMESPACE_END
#endif   // SKL_ABIX_ATOMIC_H