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

// Retired objects carry one of two deferral policies, because two consumers want
// different things out of the same grace period:
//
//   * `retire()`       - batched. Collected when `synchronize()` advances the
//                        epoch, which it only does every
//                        SKL_ABIX_RCU_EPOCH_BATCH calls. This is what keeps a
//                        reloaded module's image mapped for a while, so alternating
//                        between two module files keeps hitting the loader's cheap
//                        "already resident" path.
//   * `retire_eager()` - collected by `synchronize_eager()`, i.e. by the next one.
//                        A retired index slot array is only worth keeping if the
//                        *next* `build` can reuse its buffer, and that build is one
//                        reload away, not eight.
//
// Both classes wait for the same grace period (readers passed); only the
// collection trigger differs, and each class tracks its own "collected up to"
// watermark so one class cannot swallow the other's safe epoch.
enum class ReclaimClass : uint8_t {
    batched = 0,
    eager = 1
};

struct ThreadState {
    uint64_t epoch = 0;
    ThreadState *next = nullptr;
};

struct RetiredNode {
    void *object;
    uint64_t epoch;
    ReclaimFn reclaim;
    ReclaimClass cls;
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
    uint64_t completed_eager = 0;
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

    void retire(void *object, ReclaimFn reclaim) noexcept { retire_impl(object, reclaim, ReclaimClass::batched); }

    // Reclaimed on the next `synchronize_eager()` rather than on the next batched
    // epoch advancement. Use it for objects whose only value is becoming reusable
    // soon (a retired index slot array); use `retire` for objects whose deferral is
    // itself useful (a module image that keeps a file mapped).
    void retire_eager(void *object, ReclaimFn reclaim) noexcept { retire_impl(object, reclaim, ReclaimClass::eager); }

    void synchronize() noexcept;

    // Advances the epoch now instead of every SKL_ABIX_RCU_EPOCH_BATCH calls and
    // reclaims the eager class only, so it does not shorten the deferral the
    // batched class was tuned for. Cheap when there is nothing to collect.
    void synchronize_eager() noexcept;

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
            collect(min_epoch - 1, ReclaimClass::eager);
            collect(min_epoch - 1, ReclaimClass::batched);
            unlock_writer();
        }
    }

    uint64_t global_epoch() const noexcept { return util::load_relaxed(&_epoch.global); }

private:
    void retire_impl(void *object, ReclaimFn reclaim, ReclaimClass cls) noexcept {
        uint64_t current_epoch = util::load_relaxed(&_epoch.global);
        auto *r = new (std::nothrow) RetiredNode{object, current_epoch, reclaim, cls, nullptr};
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

    // There is deliberately no `unregister_thread`: the reader scan below walks
    // `next` without the writer lock, so unlinking a node while another thread scans
    // would be a use-after-free. A thread that has left therefore stays in the list
    // with `epoch == 0`, which never blocks a grace period; the list only grows with
    // the number of threads that ever entered a read-side section.

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

    // Reads the reader list without holding the writer lock and reports whether every
    // reader that could still be traversing objects retired at or before `target` has
    // left. A thread that entered *after* an advancement holds an epoch above
    // `target` and passes immediately, so continuously arriving readers cannot hold a
    // grace period open; only readers that were already inside (or that entered
    // before the advancement) block it.
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

    // Shared body of the two synchronize entry points; `eager` selects which class
    // is collected (and skips the batch counter).
    void synchronize_impl(bool eager) noexcept;

    // ── Collector ────────────────────────────────────────────

    // Reclaims the retired nodes of one class whose epoch is at or below
    // `safe_epoch`, i.e. every reader that could still be traversing them has
    // passed. Each class has its own watermark: a batched collect must not mark the
    // eager class as done (and vice versa), or the other class's pending nodes
    // would never be looked at again.
    void collect(uint64_t safe_epoch, ReclaimClass cls) noexcept {
        uint64_t &completed = (cls == ReclaimClass::eager) ? _epoch.completed_eager : _epoch.completed;
        if (safe_epoch <= completed) return;
        completed = safe_epoch;

        RetiredNode **prev = &_reader.retired;
        RetiredNode *curr = _reader.retired;

        while (curr) {
            if (curr->cls == cls && curr->epoch <= safe_epoch) {
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

inline void Domain::synchronize_impl(bool eager) noexcept {
    lock_writer();
    publish_local_batch_locked();

    if (util::load_relaxed(&_epoch.sync) != 0) {
        unlock_writer();
        while (util::load_acquire(&_epoch.sync) != 0) {}
        return;
    }

    if (!eager) {
        ++_sync_count;
        if (_sync_count < SKL_ABIX_RCU_EPOCH_BATCH) {
            unlock_writer();
            return;
        }
        _sync_count = 0;
    }

    uint64_t old_epoch = util::load_relaxed(&_epoch.global);
    util::inc_acq_rel(&_epoch.global);
    util::store_release(&_epoch.sync, old_epoch + 1);
    unlock_writer();

    while (!readers_passed(old_epoch)) {}

    lock_writer();
    collect(old_epoch, eager ? ReclaimClass::eager : ReclaimClass::batched);
    util::store_release(&_epoch.sync, 0);
    unlock_writer();
}

inline void Domain::synchronize() noexcept { synchronize_impl(false); }

inline void Domain::synchronize_eager() noexcept { synchronize_impl(true); }

}   // namespace skl::abix::rcu

#endif   // SKL_ABIX_RCU_DOMAIN_H
