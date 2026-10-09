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

#include "ABIX/Util/Atomic.h"

#ifndef SKL_ABIX_RCU_EPOCH_BATCH
#  define SKL_ABIX_RCU_EPOCH_BATCH 8
#endif
#ifndef SKL_ABIX_CACHE_LINE_SIZE
#  define SKL_ABIX_CACHE_LINE_SIZE 64
#endif
#ifndef SKL_ABIX_RCU_BATCH_PUBLISH
#  define SKL_ABIX_RCU_BATCH_PUBLISH 64
#endif

namespace skl::abix::rcu {

using ReclaimFn = void (*)(void *object) noexcept;

struct ThreadState {
    uint64_t epoch = 0;
    ThreadState *next = nullptr;
};

struct RetiredNode {
    void *object;
    uint64_t epoch;
    ReclaimFn reclaim;
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

class Domain {
public:
    static Domain &instance() noexcept {
        static Domain inst;
        return inst;
    }

    void enter() noexcept {
        auto *t = get_thread();
        const uint64_t epoch = util::load_acquire(&_epoch.global);
        util::store_release(&t->epoch, epoch);
    }

    void exit() noexcept { util::store_release(&get_thread()->epoch, 0ULL); }

    void retire(void *object, ReclaimFn reclaim) noexcept {
        uint64_t current_epoch = util::load_relaxed(&_epoch.global);
        auto *r = new (std::nothrow) RetiredNode{object, current_epoch, reclaim, nullptr};
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

        uint64_t current_epoch = util::load_relaxed(&_epoch.global);
        unlock_writer();

        uint64_t min_epoch = current_epoch;
        for (ThreadState *t = load_threads(); t; t = t->next) {
            const uint64_t e = util::load_acquire(&t->epoch);
            if (e != 0 && e < min_epoch) min_epoch = e;
        }

        if (min_epoch > 1) {
            lock_writer();
            collect(min_epoch - 1);
            unlock_writer();
        }
    }

    uint64_t global_epoch() const noexcept { return util::load_relaxed(&_epoch.global); }

private:
    ThreadState *get_thread() noexcept {
        struct wrapper {
            ThreadState *t = nullptr;
            ~wrapper() {
                if (t) util::store_release(&t->epoch, 0ULL);
            }
        };

        thread_local wrapper w;
        if (w.t) return w.t;

        w.t = new (std::nothrow) ThreadState{};
        if (!w.t) abort();

        register_thread(w.t);
        return w.t;
    }

    void register_thread(ThreadState *t) noexcept {
        lock_writer();
        t->next = load_threads();
        util::store_release(&_reader.threads, t);
        unlock_writer();
    }

    void unregister_thread(ThreadState *t) noexcept {
        lock_writer();
        ThreadState **prev = &_reader.threads;
        ThreadState *curr = _reader.threads;
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

    static RetiredBatch &local_batch() noexcept {
        thread_local RetiredBatch batch{};
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
            const uint64_t epoch = util::load_acquire(&t->epoch);
            if (epoch != 0 && epoch <= target) return false;
        }
        return true;
    }

    // util acquire load of the _threads list head.
    ThreadState *load_threads() noexcept { return util::load_acquire(&_reader.threads); }

    void lock_writer() noexcept {
        while (util::exchange_acq_rel(&_writer.lock, 1U) != 0U)
            while (util::load_relaxed(&_writer.lock) != 0U) {}
    }

    void unlock_writer() noexcept { util::store_release(&_writer.lock, 0U); }

    bool try_lock_writer() noexcept { return util::exchange_acq_rel(&_writer.lock, 1U) == 0U; }

    // ── Collector ────────────────────────────────────────────

    void collect(uint64_t safe_epoch) noexcept {
        if (safe_epoch <= _epoch.completed) return;
        _epoch.completed = safe_epoch;

        RetiredNode **prev = &_reader.retired;
        RetiredNode *curr = _reader.retired;

        while (curr) {
            if (curr->epoch <= safe_epoch) {
                *prev = curr->next;
                RetiredNode *next = curr->next;
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
        ThreadState *threads = nullptr;
        RetiredNode *retired = nullptr;
    };

    struct writer {
        uint64_t lock = 0;
    };

    Epoch _epoch;   // Independent cache line (64 bytes)

    reader _reader;
    writer _writer;
    uint32_t _sync_count = 0;
};

class LockGuard {
public:
    explicit LockGuard(Domain &d) noexcept
        : _Domain(d) {
        _Domain.enter();
    }
    ~LockGuard() noexcept { _Domain.exit(); }
    LockGuard(const LockGuard &) noexcept = delete;
    LockGuard &operator=(const LockGuard &) noexcept = delete;

private:
    Domain &_Domain;
};

inline void Domain::synchronize() noexcept {
    lock_writer();
    publish_local_batch_locked();

    if (util::load_relaxed(&_epoch.sync) != 0) {
        unlock_writer();
        while (util::load_acquire(&_epoch.sync) != 0) {}
        return;
    }

    ++_sync_count;
    if (_sync_count < SKL_ABIX_RCU_EPOCH_BATCH) {
        unlock_writer();
        return;
    }

    _sync_count = 0;
    uint64_t old_epoch = util::load_relaxed(&_epoch.global);
    util::inc_acq_rel(&_epoch.global);
    util::store_release(&_epoch.sync, old_epoch + 1);
    unlock_writer();

    while (!readers_passed(old_epoch)) {}

    lock_writer();
    collect(old_epoch);
    util::store_release(&_epoch.sync, 0);
    unlock_writer();
}

}   // namespace skl::abix::rcu

#endif   // SKL_ABIX_RCU_DOMAIN_H
