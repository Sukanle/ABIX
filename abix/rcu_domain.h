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
#ifndef SKL_ABIX_RCU_DOMAIN_H
#define SKL_ABIX_RCU_DOMAIN_H

#include <new>

#include "config.h"
#include "atomic.h"

SKL_ABIX_NAMESPACE_BEGIN

enum class rcu_thread_state : uint32_t {
    quiescent = 0,
    active = 1,
};

struct rcu_thread {
    uint64_t epoch;
    uint32_t state;
    rcu_thread *next;
};

using rcu_reclaim_fn = void (*)(void *object) noexcept;

struct retired_obj {
    void *object;
    uint64_t epoch;
    rcu_reclaim_fn reclaim;
    retired_obj *next;
};

class rcu_domain {
public:
    static rcu_domain &instance() noexcept {
        static rcu_domain domain;
        return domain;
    }

    void enter() noexcept {
        rcu_thread *t = get_thread();
        if (atomic::load_relaxed(&t->state) != (uint32_t)rcu_thread_state::active) {
            atomic::store_release(&t->state, (uint32_t)rcu_thread_state::active);
        }
        uint64_t epoch = atomic::load_acquire(&_global_epoch);
        atomic::store_release(&t->epoch, epoch);
    }

    void exit() noexcept {
        rcu_thread *t = get_thread();
        atomic::store_release(&t->state, (uint32_t)rcu_thread_state::quiescent);
    }

    void retire(void *object, rcu_reclaim_fn reclaim) noexcept {
        lock_writer();
        uint64_t current_epoch = atomic::load_relaxed(&_global_epoch);
        retired_obj *r = new (std::nothrow) retired_obj{object, current_epoch, reclaim, nullptr};
        if (r) {
            r->next = _retired;
            _retired = r;
        }
        unlock_writer();
    }

    void synchronize() noexcept {
        lock_writer();

        uint64_t old_epoch = atomic::inc_acq_rel(&_global_epoch) - 1;
        uint64_t new_epoch = old_epoch + 1;

        for (rcu_thread *t = (rcu_thread *)atomic::load_acquire((void **)&_threads); t; t = t->next) {
            while (atomic::load_acquire(&t->state) == (uint32_t)rcu_thread_state::active) {
                if (atomic::load_acquire(&t->epoch) == new_epoch) {
                    break;
                }
            }
        }

        collect(old_epoch);

        unlock_writer();
    }

    void try_collect() noexcept {
        if (!try_lock_writer()) return;

        uint64_t current_epoch = atomic::load_relaxed(&_global_epoch);
        uint64_t min_epoch = current_epoch;

        for (rcu_thread *t = (rcu_thread *)atomic::load_acquire((void **)&_threads); t; t = t->next) {
            if (atomic::load_acquire(&t->state) == (uint32_t)rcu_thread_state::active) {
                uint64_t e = atomic::load_acquire(&t->epoch);
                if (e < min_epoch) {
                    min_epoch = e;
                }
            }
        }

        collect(min_epoch);

        unlock_writer();
    }

private:
    static rcu_thread *&tls_thread_ref() noexcept {
        thread_local rcu_thread *t = nullptr;
        return t;
    }

    rcu_thread *get_thread() noexcept {
        rcu_thread *&tls = tls_thread_ref();
        if (tls) return tls;

        rcu_thread *t = new (std::nothrow) rcu_thread{0, (uint32_t)rcu_thread_state::quiescent, nullptr};
        if (!t) {
            static rcu_thread fallback{0, (uint32_t)rcu_thread_state::quiescent, nullptr};
            return &fallback;
        }

        lock_writer();
        t->next = _threads;
        _threads = t;
        unlock_writer();

        tls = t;
        return t;
    }

    void lock_writer() noexcept {
        while (!atomic::cas_relaxed(&_writer_lock, 0, 1)) {}
    }

    void unlock_writer() noexcept { atomic::store_relaxed(&_writer_lock, 0); }

    bool try_lock_writer() noexcept { return atomic::cas_relaxed(&_writer_lock, 0, 1); }

    void collect(uint64_t safe_epoch) noexcept {
        retired_obj **prev = &_retired;
        retired_obj *curr = _retired;

        while (curr) {
            if (curr->epoch < safe_epoch) {
                *prev = curr->next;
                retired_obj *next = curr->next;
                if (curr->reclaim && curr->object) {
                    curr->reclaim(curr->object);
                }
                delete curr;
                curr = next;
            } else {
                prev = &curr->next;
                curr = curr->next;
            }
        }
    }

    uint64_t _global_epoch = 0;
    rcu_thread *_threads = nullptr;
    retired_obj *_retired = nullptr;
    uint32_t _writer_lock = 0;
};

class rcu_guard {
public:
    explicit rcu_guard(rcu_domain &d) noexcept
        : _domain(d) {
        _domain.enter();
    }
    ~rcu_guard() noexcept { _domain.exit(); }
    rcu_guard(const rcu_guard &) = delete;
    rcu_guard &operator=(const rcu_guard &) = delete;

private:
    rcu_domain &_domain;
};

SKL_ABIX_NAMESPACE_END

#endif   // SKL_ABIX_RCU_DOMAIN_H