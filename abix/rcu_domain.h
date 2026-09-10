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

#include <stdlib.h>

#include "config.h"
#include "atomic.h"

#ifndef SKL_ABIX_RCU_EPOCH_BATCH
#  define SKL_ABIX_RCU_EPOCH_BATCH 8
#endif
#ifndef SKL_ABIX_CACHE_LINE_SIZE
#  define SKL_ABIX_CACHE_LINE_SIZE 64
#endif
#ifndef SKL_ABIX_RCU_BATCH_PUBLISH
#  define SKL_ABIX_RCU_BATCH_PUBLISH 64
#endif

SKL_ABIX_NAMESPACE_BEGIN

using rcu_reclaim_fn = void (*)(void *object) noexcept;

// These are the stable, data-only EBR state records.  They intentionally live
// outside rcu_domain so AMC can describe them before the concurrent domain is
// initialized.  rcu_domain only gives these records their concurrency meaning.
namespace runtime::ebr {

struct ThreadState {
    uint64_t epoch = 0;
    ThreadState *next = nullptr;
};

struct RetiredNode {
    void *object;
    uint64_t epoch;
    rcu_reclaim_fn reclaim;
    RetiredNode *next;
};

struct RetiredBatch {
    RetiredNode *head = nullptr;
    RetiredNode *tail = nullptr;
    uint32_t count = 0;
};

struct alignas(SKL_ABIX_CACHE_LINE_SIZE) Epoch {
    uint64_t global = 1;
    uint64_t completed = 0;
    uint64_t sync = 0;
};

}  // namespace runtime::ebr

namespace detail {

using rcu_thread = runtime::ebr::ThreadState;
using retired_obj = runtime::ebr::RetiredNode;
using rcu_retired_batch = runtime::ebr::RetiredBatch;

}   // namespace detail

class rcu_domain {
public:
    static rcu_domain &instance() noexcept {
        static rcu_domain domain;
        return domain;
    }

    void enter() noexcept {
        auto *t = get_thread();
        const uint64_t epoch = atomic::load_acquire(&_epoch.global);
        atomic::store_release(&t->epoch, epoch);
    }

    void exit() noexcept { atomic::store_release(&get_thread()->epoch, 0ULL); }

    void retire(void *object, rcu_reclaim_fn reclaim) noexcept {
        uint64_t current_epoch = atomic::load_relaxed(&_epoch.global);
        auto *r = new (std::nothrow) detail::retired_obj{object, current_epoch, reclaim, nullptr};
        if (!r) return;

        auto &batch = local_batch();
        if (batch.tail)
            batch.tail->next = r;
        else
            batch.head = r;
        batch.tail = r;
        batch.count++;

        if (batch.count >= SKL_ABIX_RCU_BATCH_PUBLISH) publish_local_batch();
    }

    void synchronize() noexcept;

    void try_collect() noexcept {
        if (!try_lock_writer()) return;

        publish_local_batch_locked();

        uint64_t current_epoch = atomic::load_relaxed(&_epoch.global);
        unlock_writer();

        uint64_t min_epoch = current_epoch;
        for (detail::rcu_thread *t = load_threads(); t; t = t->next) {
            const uint64_t e = atomic::load_acquire(&t->epoch);
            if (e != 0 && e < min_epoch) min_epoch = e;
        }

        if (min_epoch > 1) {
            lock_writer();
            collect(min_epoch - 1);
            unlock_writer();
        }
    }

    uint64_t global_epoch() const noexcept { return atomic::load_relaxed(&_epoch.global); }

private:
    detail::rcu_thread *get_thread() noexcept {
        struct wrapper {
            detail::rcu_thread *t = nullptr;
            ~wrapper() {
                if (t) atomic::store_release(&t->epoch, 0ULL);
            }
        };

        thread_local wrapper w;
        if (w.t) return w.t;

        w.t = new (std::nothrow) detail::rcu_thread{};
        if (!w.t) abort();

        register_thread(w.t);
        return w.t;
    }

    void register_thread(detail::rcu_thread *t) noexcept {
        lock_writer();
        t->next = load_threads();
        atomic::store_release(&_reader.threads, t);
        unlock_writer();
    }

    void unregister_thread(detail::rcu_thread *t) noexcept {
        lock_writer();
        detail::rcu_thread **prev = &_reader.threads;
        detail::rcu_thread *curr = _reader.threads;
        while (curr) {
            if (curr == t) {
                *prev = curr->next;
                break;
            }
            prev = &curr->next;
            curr = curr->next;
        }
        unlock_writer();
    }

    static detail::rcu_retired_batch &local_batch() noexcept {
        thread_local detail::rcu_retired_batch batch{};
        return batch;
    }

    void publish_local_batch_locked() noexcept {
        auto &batch = local_batch();
        if (!batch.head) return;

        batch.tail->next = _reader.retired;
        _reader.retired = batch.head;
        batch.head = nullptr;
        batch.tail = nullptr;
        batch.count = 0;
    }

    void publish_local_batch() noexcept {
        lock_writer();
        publish_local_batch_locked();
        unlock_writer();
    }

    bool readers_passed(uint64_t target) noexcept {
        for (auto *t = load_threads(); t; t = t->next) {
            const uint64_t epoch = atomic::load_acquire(&t->epoch);
            if (epoch != 0 && epoch <= target) return false;
        }
        return true;
    }

    // Atomic acquire load of the _threads list head.
    detail::rcu_thread *load_threads() noexcept { return atomic::load_acquire(&_reader.threads); }

    void lock_writer() noexcept {
        while (atomic::exchange_acq_rel(&_writer.lock, 1U) != 0U)
            while (atomic::load_relaxed(&_writer.lock) != 0U) {}
    }

    void unlock_writer() noexcept { atomic::store_release(&_writer.lock, 0U); }

    bool try_lock_writer() noexcept { return atomic::exchange_acq_rel(&_writer.lock, 1U) == 0U; }

    // ── Collector ────────────────────────────────────────────

    void collect(uint64_t safe_epoch) noexcept {
        if (safe_epoch <= _epoch.completed) return;
        _epoch.completed = safe_epoch;

        detail::retired_obj **prev = &_reader.retired;
        detail::retired_obj *curr = _reader.retired;

        while (curr) {
            if (curr->epoch <= safe_epoch) {
                *prev = curr->next;
                detail::retired_obj *next = curr->next;
                if (curr->reclaim && curr->object) curr->reclaim(curr->object);
                delete curr;
                curr = next;
            } else {
                prev = &curr->next;
                curr = curr->next;
            }
        }
    }

    struct reader {
        detail::rcu_thread *threads = nullptr;
        detail::retired_obj *retired = nullptr;
    };

    struct writer {
        uint64_t lock = 0;
    };

    runtime::ebr::Epoch _epoch;   // Independent cache line (64 bytes)

    reader _reader;
    writer _writer;
    uint32_t _sync_count = 0;
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

inline void rcu_domain::synchronize() noexcept {
    lock_writer();
    publish_local_batch_locked();

    if (atomic::load_relaxed(&_epoch.sync) != 0) {
        unlock_writer();
        while (atomic::load_acquire(&_epoch.sync) != 0) {}
        return;
    }

    ++_sync_count;
    if (_sync_count < SKL_ABIX_RCU_EPOCH_BATCH) {
        unlock_writer();
        return;
    }

    _sync_count = 0;
    uint64_t old_epoch = atomic::load_relaxed(&_epoch.global);
    atomic::inc_acq_rel(&_epoch.global);
    atomic::store_release(&_epoch.sync, old_epoch + 1);
    unlock_writer();

    while (!readers_passed(old_epoch)) {}

    lock_writer();
    collect(old_epoch);
    atomic::store_release(&_epoch.sync, 0);
    unlock_writer();
}

SKL_ABIX_NAMESPACE_END

#endif   // SKL_ABIX_RCU_DOMAIN_H
