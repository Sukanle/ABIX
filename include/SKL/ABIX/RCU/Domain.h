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

#include "SKL/ABIX/RCU/Deadline.h"
#include "SKL/ABIX/Util/Atomic.h"

#ifndef SKL_ABIX_RCU_EPOCH_BATCH
#  define SKL_ABIX_RCU_EPOCH_BATCH 8
#endif
#ifndef SKL_ABIX_CACHE_LINE_SIZE
#  define SKL_ABIX_CACHE_LINE_SIZE 64
#endif
#ifndef SKL_ABIX_RCU_BATCH_PUBLISH
#  define SKL_ABIX_RCU_BATCH_PUBLISH 64
#endif
// Optional advancement diagnostics. Off by default: when it is not defined every
// counter update compiles away (they sit behind `if constexpr`), so the default
// build keeps the same reader path and the same advancement path. Enabling it adds
// a handful of relaxed increments on the writer side only, plus one clock reading
// per retired node so `retired_oldest_age_ns` can be reported.
#ifndef SKL_ABIX_RCU_ENABLE_STATS
#  define SKL_ABIX_RCU_ENABLE_STATS 0
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
    // Monotonic clock reading taken when the node was queued. Only written when
    // SKL_ABIX_RCU_ENABLE_STATS is on; the field stays in the layout either way so
    // enabling diagnostics never changes the node's ABI.
    uint64_t retired_at_ns = 0;
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
    // Optional advancement diagnostics (see SKL_ABIX_RCU_ENABLE_STATS).
    //
    // These exist to answer one question before any fairness mechanism is built:
    // does a writer actually starve on the advancement right, or is the current
    // preemptive retry loop good enough in practice? Two failure modes that a
    // single "RCU timeout" number would conflate are kept apart:
    //
    //   * advance_timeouts      - never got the advancement right: another writer
    //                             kept `sync` for the whole deadline.
    //   * grace_period_timeouts - owned the advancement, but a reader did not
    //                             leave before the deadline.
    //   * deferred_batched is not a timeout at all and is reported by the Object
    //     layer (`ReclaimOutcome`), not here.
    //
    // `registered_threads`, `retired_backlog` and `retired_oldest_age_ns` are gauges
    // sampled from the domain's lists by `stats()`, not counters.
    struct Stats {
        uint64_t advance_attempts = 0;        // times a thread tried to take the advancement
        uint64_t advance_contentions = 0;     // attempts that found another owner in `sync`
        uint64_t advance_retries = 0;         // failed advancement attempts retried by synchronize_until
        uint64_t advance_timeouts = 0;        // gave up waiting for the advancement at the deadline
        uint64_t advance_max_wait_ns = 0;     // longest observed wait for the advancement
        uint64_t grace_period_timeouts = 0;   // owned the advancement, did not prove the grace period
        uint64_t retired_backlog = 0;         // nodes still queued at the time of the snapshot
        uint64_t retired_oldest_age_ns = 0;   // age of the oldest queued node at the snapshot
        // Threads that ever entered a read-side section. Monotonically non-decreasing:
        // a thread that leaves keeps its node with `epoch == 0` (see readers_passed), and
        // every grace-period poll walks all of them. Watch this on a host with thread
        // churn; the plan to bound it is a fixed-capacity slot array (roadmap, "Research").
        uint64_t registered_threads = 0;
    };

    // Whether this build collects the diagnostics above. When false, `stats()`
    // returns an all-zero snapshot without taking the writer lock.
    static constexpr bool stats_enabled() noexcept { return SKL_ABIX_RCU_ENABLE_STATS != 0; }

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

    // The epoch a retired object was queued at ("token"). Together with
    // `reclaimed_through` it answers "has the domain already released that object?"
    // without the caller having to observe its own reclaim callback.
    uint64_t retire_token(void *object, ReclaimFn reclaim) noexcept {
        return retire_impl(object, reclaim, ReclaimClass::batched);
    }

    // Whether the given class has already been collected through `token`, i.e. every
    // object retired at or before that epoch has had its reclaim callback run.
    // Monotonic per class, and only ever advanced after a grace period was proven.
    bool reclaimed_through(uint64_t token, ReclaimClass cls) const noexcept {
        const uint64_t completed = (cls == ReclaimClass::eager) ? util::load_acquire(&_epoch.completed_eager)
                                                                : util::load_acquire(&_epoch.completed);
        return token != 0 && completed >= token;
    }

    void synchronize() noexcept;

    // Advances the epoch now instead of every SKL_ABIX_RCU_EPOCH_BATCH calls and
    // reclaims the eager class only, so it does not shorten the deferral the
    // batched class was tuned for. Cheap when there is nothing to collect.
    void synchronize_eager() noexcept;

    // Requests one grace period and waits for it at most until `deadline`.
    //
    //   true  - the requested grace period **was proven**: every reader that could
    //           still be traversing objects retired before the advancement has left
    //           its critical section. Nothing more: this does not promise that a
    //           collection scan ran, and it does not promise that any particular
    //           reclaim callback has executed (`reclaimed_through` answers that).
    //   false - the grace period was **not proven** within the deadline. Either the
    //           deadline expired while a reader was still inside, or this thread never
    //           got to perform the advancement because another writer held it. A false
    //           result says nothing about whether readers have left, it never advances
    //           the reclaimed-through watermark, and it never runs a reclaim callback,
    //           so a caller must not release anything on the strength of it.
    //
    // Unlike `synchronize()` this always asks for a real grace period (it does not
    // take the batched early return), which is the documented trade of enabling a
    // bounded wait. The epoch advancement is irreversible, so a timeout still leaves
    // the domain consistent and later synchronizations keep working.
    bool synchronize_until(Deadline deadline) noexcept;

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

    // Snapshot of the optional diagnostics. The counters are read with relaxed
    // loads (they are advisory, not a synchronisation primitive); the two gauges
    // are taken under the writer lock so the retired list cannot change while it
    // is walked. Cheap enough for a diagnostics endpoint, not for a hot loop.
    Stats stats() noexcept {
        Stats snapshot;
        if constexpr (stats_enabled()) {
            lock_writer();
            snapshot.advance_attempts = util::load_relaxed(&_counters.advance_attempts);
            snapshot.advance_contentions = util::load_relaxed(&_counters.advance_contentions);
            snapshot.advance_retries = util::load_relaxed(&_counters.advance_retries);
            snapshot.advance_timeouts = util::load_relaxed(&_counters.advance_timeouts);
            snapshot.advance_max_wait_ns = util::load_relaxed(&_counters.advance_max_wait_ns);
            snapshot.grace_period_timeouts = util::load_relaxed(&_counters.grace_period_timeouts);

            const uint64_t now = detail::monotonic_now_ns();
            for (const RetiredNode *n = _reader.retired; n; n = n->next) {
                ++snapshot.retired_backlog;
                if (n->retired_at_ns != 0) {
                    const uint64_t age = now - n->retired_at_ns;
                    if (age > snapshot.retired_oldest_age_ns) snapshot.retired_oldest_age_ns = age;
                }
            }
            // Appends to the thread list also happen under the writer lock, so this
            // walk is exclusive with a new registration.
            for (const ThreadState *t = _reader.threads; t; t = t->next)
                ++snapshot.registered_threads;
            unlock_writer();
        }
        return snapshot;
    }

    // Zeroes the counters. The gauges need no reset: they are computed on read.
    void reset_stats() noexcept {
        if constexpr (stats_enabled()) {
            lock_writer();
            util::store_relaxed(&_counters.advance_attempts, 0ULL);
            util::store_relaxed(&_counters.advance_contentions, 0ULL);
            util::store_relaxed(&_counters.advance_retries, 0ULL);
            util::store_relaxed(&_counters.advance_timeouts, 0ULL);
            util::store_relaxed(&_counters.advance_max_wait_ns, 0ULL);
            util::store_relaxed(&_counters.grace_period_timeouts, 0ULL);
            unlock_writer();
        }
    }

private:
    // Queues an object for reclamation and returns the epoch it was queued at, or 0
    // when the node could not be allocated (nothing was queued in that case).
    uint64_t retire_impl(void *object, ReclaimFn reclaim, ReclaimClass cls) noexcept {
        uint64_t current_epoch = util::load_relaxed(&_epoch.global);
        auto *r = new (std::nothrow) RetiredNode{object, current_epoch, reclaim, cls, nullptr};
        if (!r) return 0;
        if constexpr (stats_enabled()) {
            r->retired_at_ns = detail::monotonic_now_ns();
        }

        auto &batch = local_batch();
        if (batch.tail)
            batch.tail->next = r;
        else
            batch.head = r;
        batch.tail = r;
        batch.count++;

        if (batch.count >= SKL_ABIX_RCU_BATCH_PUBLISH) publish_local_batch();
        return current_epoch;
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

    // Takes ownership of one epoch advancement. Returns false when another writer
    // already owns it (the caller must not touch `sync` in that case). The owner is
    // responsible for clearing `sync` on every path out.
    //
    // This is a preemptive attempt, not a fair one: there is no queue and no
    // ordering, so a retrying caller can lose indefinitely to later writers. That
    // is a documented property, not a bug - see `Stats` and the D-1 notes in
    // memory/Optimize-plan.md for why fairness is measured before it is implemented.
    bool begin_advance(uint64_t &old_epoch) noexcept {
        lock_writer();
        publish_local_batch_locked();
        if constexpr (stats_enabled()) {
            util::inc_relaxed(&_counters.advance_attempts);
        }
        if (util::load_relaxed(&_epoch.sync) != 0) {
            if constexpr (stats_enabled()) {
                util::inc_relaxed(&_counters.advance_contentions);
            }
            unlock_writer();
            return false;
        }
        old_epoch = util::load_relaxed(&_epoch.global);
        util::inc_acq_rel(&_epoch.global);
        util::store_release(&_epoch.sync, old_epoch + 1);
        unlock_writer();
        return true;
    }

    // Clears `sync` after an advancement owned by this thread, optionally collecting
    // both classes. A proven grace period covers both, so a bounded wait reclaims the
    // eager class (index buffers) and the batched class (images) in one pass.
    void end_advance(uint64_t old_epoch, bool collect_now) noexcept {
        lock_writer();
        if (collect_now) {
            collect(old_epoch, ReclaimClass::eager);
            collect(old_epoch, ReclaimClass::batched);
        }
        util::store_release(&_epoch.sync, 0);
        unlock_writer();
    }

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

    // Declared unconditionally, and on its own cache line, so enabling the
    // diagnostics never changes the domain's layout - only whether the fields are
    // written. The counters live on the writer/advancement path, so they must not
    // share a line with the Epoch block.
    struct alignas(SKL_ABIX_CACHE_LINE_SIZE) Counters {
        uint64_t advance_attempts = 0;
        uint64_t advance_contentions = 0;
        uint64_t advance_retries = 0;
        uint64_t advance_timeouts = 0;
        uint64_t advance_max_wait_ns = 0;
        uint64_t grace_period_timeouts = 0;
    };

    // Raises `advance_max_wait_ns` to `ns` if it is a new maximum. A CAS loop
    // rather than a fetch-max so concurrent writers do not lose a larger sample.
    void note_max_wait_ns(uint64_t ns) noexcept {
        uint64_t observed = util::load_relaxed(&_counters.advance_max_wait_ns);
        while (ns > observed && !util::compare_exchange(&_counters.advance_max_wait_ns, observed, ns)) {}
    }

    Epoch _epoch;   // Independent cache line (64 bytes)

    reader _reader;
    writer _writer;
    uint32_t _sync_count = 0;
    Counters _counters;
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
        // Counted as an attempt: the caller asked for a synchronize and found the
        // advancement held. (For the batched entry point we cannot know whether the
        // batch gate would have let it advance without disturbing `_sync_count`, so
        // this is counted as an attempt even when the cadence was not yet due.)
        if constexpr (stats_enabled()) {
            util::inc_relaxed(&_counters.advance_attempts);
            util::inc_relaxed(&_counters.advance_contentions);
        }
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

    if constexpr (stats_enabled()) {
        util::inc_relaxed(&_counters.advance_attempts);
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

inline bool Domain::synchronize_until(Deadline deadline) noexcept {
    // Diagnostics: the wait for the advancement is timed from the first contended
    // attempt, so a first-try success records nothing (there was no wait). Only the
    // bounded caller is timed at all; the unbounded `synchronize()` path does not
    // pay for this.
    bool waiting = false;
    uint64_t wait_start_ns = 0;

    for (;;) {
        uint64_t old_epoch = 0;
        if (!begin_advance(old_epoch)) {
            // Another writer owns the advancement: this thread waits, retries, or
            // gives up, and it never clears a `sync` it does not own.
            if constexpr (stats_enabled()) {
                if (!waiting) {
                    waiting = true;
                    wait_start_ns = detail::monotonic_now_ns();
                }
                util::inc_relaxed(&_counters.advance_retries);
            }
            if (deadline.bounded() && deadline.expired()) {
                // Timed out waiting for the advancement itself: distinct from a
                // grace-period timeout, which means the advancement was ours.
                if constexpr (stats_enabled()) {
                    util::inc_relaxed(&_counters.advance_timeouts);
                    note_max_wait_ns(detail::monotonic_now_ns() - wait_start_ns);
                }
                return false;
            }
            continue;
        }

        if constexpr (stats_enabled()) {
            if (waiting) note_max_wait_ns(detail::monotonic_now_ns() - wait_start_ns);
        }

        // This thread owns the advancement from here on and must clear `sync` on
        // every path out. The clock is polled rather than read per iteration so an
        // unbounded wait costs nothing: `expired()` on a `never()` deadline is a
        // single register compare.
        unsigned polls = 0;
        bool proven = true;
        while (!readers_passed(old_epoch)) {
            if ((++polls & 0xFFu) == 0 && deadline.expired()) {
                proven = false;
                break;
            }
        }

        if constexpr (stats_enabled()) {
            if (!proven) util::inc_relaxed(&_counters.grace_period_timeouts);
        }

        // On a proven grace period both classes are collected (the return value still
        // promises only the grace period: `collect` may early-out on its watermark, and
        // callbacks run for as long as they run).
        end_advance(old_epoch, /*collect_now=*/proven);
        return proven;
    }
}

}   // namespace skl::abix::rcu

#endif   // SKL_ABIX_RCU_DOMAIN_H
