// ============================================================
// Probe: does a writer actually starve on the RCU advancement right?
//
// Advancement (who gets to run a grace period) is preemptive and has no
// fairness guarantee: there is no queue and no ordering, so a writer that keeps
// losing the race to later writers can lose for as long as they keep arriving.
// This probe makes that observable instead of assumed, so the "measure first,
// then decide whether to build a cancellable FIFO arbitration" plan has data.
//
// It is a plain std::chrono harness rather than a Google Benchmark one, because
// the answer is a distribution over a wall-clock window (per-writer success
// share, backlog trend, longest timeout streak), not a per-operation cost.
//
// Requires SKL_ABIX_RCU_ENABLE_STATS (forced on for this target): the domain
// counters are what distinguish the two failure modes a single "timeout" number
// would conflate:
//   advance_timeouts      - never got the advancement right (another writer held sync)
//   grace_period_timeouts - owned it, but a reader did not leave
//
// Usage:
//   rcu_advance_probe [options]
//     --writers N        competing writer threads              (default 4)
//     --readers N        reader threads entering/exiting RCU    (default 0)
//     --hold-us U        how long a reader stays inside per visit (default 0)
//     --deadline-ms M    per-call wait bound; 0 = non-blocking probe (default 1)
//     --duration-ms D    wall-clock run window                  (default 2000)
//     --sample-ms T      stats sampling period                  (default 200)
//     --retire-per-call K  nodes retired per raw-mode iteration (default 1)
//     --max-backlog K    stop writers if published backlog reaches K (default 250000)
//     --reload [plugin]  drive the real Object::reload path instead of the raw
//                        Domain (plugin default "reload_dll_a")
//     --help
//
// A held reader (--hold-us > 0) makes the grace period unprovable, so the backlog
// grows without bound by design (Safe: defer, never leak) until --max-backlog
// stops the writers; the drain line then reports whether it fell back to 0 once
// the readers left. Raise --max-backlog to watch a longer trend.
//
// Keep --writers + --readers at or below hardware_concurrency: above it, a thread
// can be descheduled for milliseconds and the success spread measures the OS
// scheduler, not the advancement protocol. The probe warns when it is oversubscribed.
//
// Exit code: 0 no anomaly, 1 suspected starvation (a writer never succeeded
// while others clearly did), 2 the handshake could not be drained after the
// readers stopped, 3 a reload failed. A run where *nobody* advances because a
// reader is held is not starvation: it is a shared blocker, reported as such.
// ============================================================

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "ABIX/ABIX.h"
#include "dlls/plugin_path.h"

namespace {

uint64_t now_ns() noexcept {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

void reclaim_int(void *p) noexcept { delete static_cast<int *>(p); }

struct Options {
    int writers = 4;
    int readers = 0;
    uint64_t hold_us = 0;
    uint64_t deadline_ms = 1;
    uint64_t duration_ms = 2'000;
    uint64_t sample_ms = 200;
    uint64_t retire_per_call = 1;
    uint64_t max_backlog = 250'000;
    bool reload = false;
    std::string plugin = "reload_dll_a";
};

struct WriterReport {
    uint64_t ok = 0;
    uint64_t timeouts = 0;
    uint64_t max_streak = 0;   // longest run of consecutive timeouts
    uint64_t max_ns = 0;       // longest single call that succeeded
    bool failed = false;       // reload mode: load/reload returned false
};

skl::abix::rcu::Deadline make_deadline(uint64_t ms) {
    return ms == 0 ? skl::abix::rcu::Deadline::after_ms(0) : skl::abix::rcu::Deadline::after_ms(ms);
}

void reader_loop(skl::abix::rcu::Domain &dom, std::atomic<bool> &running, uint64_t hold_ns) {
    while (running.load(std::memory_order_relaxed)) {
        dom.enter();
        if (hold_ns) {
            const uint64_t start = now_ns();
            while (now_ns() - start < hold_ns && running.load(std::memory_order_relaxed)) {}
        }
        dom.exit();
    }
}

// Raw mode: the RCU footprint of a reload (retire the old image, then one
// bounded advancement) without the loader in the way.
void raw_writer_loop(skl::abix::rcu::Domain &dom, skl::abix::rcu::Deadline deadline, std::atomic<bool> &running,
    uint64_t retire_per_call, WriterReport &rep) {
    uint64_t streak = 0;
    while (running.load(std::memory_order_relaxed)) {
        for (uint64_t i = 0; i < retire_per_call; ++i)
            dom.retire(new (std::nothrow) int(1), reclaim_int);

        const uint64_t start = now_ns();
        const bool proven = dom.synchronize_until(deadline);
        const uint64_t elapsed = now_ns() - start;
        if (proven) {
            ++rep.ok;
            streak = 0;
            if (elapsed > rep.max_ns) rep.max_ns = elapsed;
        } else {
            ++rep.timeouts;
            if (++streak > rep.max_streak) rep.max_streak = streak;
        }
    }
}

// Reload mode: the real path. Each writer owns its Object (concurrent reload of
// the same Object is not an API promise) but they share the one Domain, which is
// what the advancement is contended on.
void reload_writer_loop(std::atomic<bool> &running, uint64_t deadline_ms, const std::string &path, WriterReport &rep) {
    skl::abix::dll::Object lib;
    if (!lib.load(path.c_str())) {
        rep.failed = true;
        return;
    }
    lib.set_reclaim_timeout_ms(deadline_ms);

    uint64_t streak = 0;
    while (running.load(std::memory_order_relaxed)) {
        const uint64_t start = now_ns();
        if (!lib.reload(path.c_str())) {
            rep.failed = true;
            break;
        }
        const uint64_t elapsed = now_ns() - start;
        if (lib.reclaim_outcome() == skl::abix::dll::ReclaimOutcome::deferred_timeout) {
            ++rep.timeouts;
            if (++streak > rep.max_streak) rep.max_streak = streak;
        } else {
            ++rep.ok;
            streak = 0;
            if (elapsed > rep.max_ns) rep.max_ns = elapsed;
        }
    }
}

void print_help() {
    std::printf(
        "rcu_advance_probe [options]\n"
        "  --writers N          competing writer threads            (default 4)\n"
        "  --readers N          reader threads entering/exiting RCU  (default 0)\n"
        "  --hold-us U          reader stays inside per visit, us    (default 0)\n"
        "  --deadline-ms M      per-call wait bound, 0 = non-blocking (default 1)\n"
        "  --duration-ms D      wall-clock run window                (default 2000)\n"
        "  --sample-ms T        stats sampling period                (default 200)\n"
        "  --retire-per-call K  nodes retired per raw iteration      (default 1)\n"
        "  --max-backlog K      stop writers at K published nodes    (default 250000)\n"
        "  --reload [plugin]    drive Object::reload (default reload_dll_a)\n"
        "  --help\n");
}

}   // namespace

int main(int argc, char **argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const bool has_next = (i + 1) < argc;
        if (std::strcmp(a, "--help") == 0) {
            print_help();
            return 0;
        } else if (std::strcmp(a, "--writers") == 0 && has_next) {
            opt.writers = std::atoi(argv[++i]);
        } else if (std::strcmp(a, "--readers") == 0 && has_next) {
            opt.readers = std::atoi(argv[++i]);
        } else if (std::strcmp(a, "--hold-us") == 0 && has_next) {
            opt.hold_us = std::strtoull(argv[++i], nullptr, 10);
        } else if (std::strcmp(a, "--deadline-ms") == 0 && has_next) {
            opt.deadline_ms = std::strtoull(argv[++i], nullptr, 10);
        } else if (std::strcmp(a, "--duration-ms") == 0 && has_next) {
            opt.duration_ms = std::strtoull(argv[++i], nullptr, 10);
        } else if (std::strcmp(a, "--sample-ms") == 0 && has_next) {
            opt.sample_ms = std::strtoull(argv[++i], nullptr, 10);
        } else if (std::strcmp(a, "--retire-per-call") == 0 && has_next) {
            opt.retire_per_call = std::strtoull(argv[++i], nullptr, 10);
        } else if (std::strcmp(a, "--max-backlog") == 0 && has_next) {
            opt.max_backlog = std::strtoull(argv[++i], nullptr, 10);
        } else if (std::strcmp(a, "--reload") == 0) {
            opt.reload = true;
            if (has_next && argv[i + 1][0] != '-') opt.plugin = argv[++i];
        } else {
            std::fprintf(stderr, "unknown option: %s (try --help)\n", a);
            return 64;
        }
    }
    if (opt.writers <= 0) {
        std::fprintf(stderr, "--writers must be >= 1\n");
        return 64;
    }
    if (opt.sample_ms == 0) opt.sample_ms = 1;
    if (opt.reload && opt.deadline_ms == 0) {
        // For Object a 0 timeout means *unbounded*, not a non-blocking probe, so a
        // held reader would block the reload forever and the probe could not stop.
        std::fprintf(stderr, "--reload needs --deadline-ms >= 1 (0 means unbounded for Object)\n");
        return 64;
    }

    auto &dom = skl::abix::rcu::Domain::instance();
    if (!skl::abix::rcu::Domain::stats_enabled()) {
        std::fprintf(stderr, "this binary was built without SKL_ABIX_RCU_ENABLE_STATS\n");
        return 64;
    }

    const auto deadline = make_deadline(opt.deadline_ms);
    const uint64_t hold_ns = opt.hold_us * 1'000ULL;
    const std::string path = opt.reload ? dll_path(opt.plugin.c_str()) : std::string{};

    const unsigned hw = std::thread::hardware_concurrency();
    const int threads = opt.writers + opt.readers;

    std::printf("rcu_advance_probe\n");
    std::printf("  mode=%s writers=%d readers=%d hold_us=%llu deadline_ms=%llu duration_ms=%llu retire_per_call=%llu\n",
        opt.reload ? "reload" : "raw", opt.writers, opt.readers, (unsigned long long)opt.hold_us,
        (unsigned long long)opt.deadline_ms, (unsigned long long)opt.duration_ms,
        (unsigned long long)opt.retire_per_call);
    if (opt.reload) std::printf("  plugin=%s\n", path.c_str());
    std::printf("  hardware_concurrency=%u\n", hw);
    if (hw != 0 && static_cast<unsigned>(threads) > hw) {
        // Under oversubscription a thread can be descheduled for milliseconds, which
        // shows up as one writer losing many races in a row. That is the scheduler,
        // not the advancement protocol, so do not read the spread below as starvation
        // without re-running at or below hardware_concurrency.
        std::printf(
            "  WARNING: %d threads > %u hw threads (oversubscribed): success spread and timeout streaks may reflect "
            "OS scheduling, not the advancement protocol\n",
            threads, hw);
    }

    dom.reset_stats();

    std::atomic<bool> running{true};
    std::vector<WriterReport> reports(static_cast<size_t>(opt.writers));
    std::vector<std::thread> workers;

    for (int i = 0; i < opt.readers; ++i)
        workers.emplace_back(reader_loop, std::ref(dom), std::ref(running), hold_ns);

    for (int i = 0; i < opt.writers; ++i) {
        if (opt.reload)
            workers.emplace_back(reload_writer_loop, std::ref(running), opt.deadline_ms, std::cref(path),
                std::ref(reports[static_cast<size_t>(i)]));
        else
            workers.emplace_back(raw_writer_loop, std::ref(dom), deadline, std::ref(running), opt.retire_per_call,
                std::ref(reports[static_cast<size_t>(i)]));
    }

    // Sample the domain while the window is open: the backlog trend is the
    // "retired queue accumulating?" half of the question. The cap is polled more
    // often than the report line is printed, because a held reader grows the
    // backlog unbounded by design (Safe: defer, never leak) and the probe should
    // not exhaust memory before it is stopped.
    const uint64_t window_start = now_ns();
    const uint64_t window_ns = opt.duration_ms * 1'000'000ULL;
    const uint64_t poll_ns = 50'000'000ULL;   // 50 ms: the cap check cadence
    uint64_t last_report_ns = 0;
    uint64_t max_backlog = 0;
    uint64_t max_oldest_age_ns = 0;
    int samples = 0;
    bool capped = false;
    while (now_ns() - window_start < window_ns) {
        const uint64_t elapsed = now_ns() - window_start;
        const uint64_t remaining = window_ns - elapsed;
        std::this_thread::sleep_for(std::chrono::microseconds(std::min<uint64_t>(poll_ns, remaining) / 1'000ULL));

        const auto s = dom.stats();
        if (s.retired_backlog > max_backlog) max_backlog = s.retired_backlog;
        if (s.retired_oldest_age_ns > max_oldest_age_ns) max_oldest_age_ns = s.retired_oldest_age_ns;

        if (opt.max_backlog != 0 && s.retired_backlog >= opt.max_backlog) {
            capped = true;
            running.store(false, std::memory_order_relaxed);
            std::printf("  backlog cap reached (%llu >= %llu): stopping writers early\n",
                (unsigned long long)s.retired_backlog, (unsigned long long)opt.max_backlog);
            break;
        }

        if (now_ns() - window_start - last_report_ns >= opt.sample_ms * 1'000'000ULL) {
            last_report_ns = now_ns() - window_start;
            ++samples;
            std::printf(
                "  t=%6llu ms  backlog=%7llu oldest=%10llu ns  attempts=%-9llu contend=%-9llu retries=%-10llu "
                "adv_to=%-8llu grace_to=%-8llu max_wait=%10llu ns\n",
                (unsigned long long)(last_report_ns / 1'000'000ULL), (unsigned long long)s.retired_backlog,
                (unsigned long long)s.retired_oldest_age_ns, (unsigned long long)s.advance_attempts,
                (unsigned long long)s.advance_contentions, (unsigned long long)s.advance_retries,
                (unsigned long long)s.advance_timeouts, (unsigned long long)s.grace_period_timeouts,
                (unsigned long long)s.advance_max_wait_ns);
        }
    }

    running.store(false, std::memory_order_relaxed);
    for (auto &w : workers)
        w.join();

    const auto totals = dom.stats();

    // Drain: with every reader stopped, one bounded advancement must prove and
    // collect. A failure here is a wedged handshake, not a fairness problem.
    const bool drained = dom.synchronize_until(skl::abix::rcu::Deadline::after_ms(5'000));
    const auto after_drain = dom.stats();

    std::printf("\n== per writer ==\n");
    uint64_t total_ok = 0;
    uint64_t min_ok = UINT64_MAX;
    uint64_t max_ok = 0;
    uint64_t max_streak = 0;
    uint64_t worst_call_ns = 0;
    bool any_failed = false;
    for (int i = 0; i < opt.writers; ++i) {
        const auto &r = reports[static_cast<size_t>(i)];
        total_ok += r.ok;
        if (r.ok < min_ok) min_ok = r.ok;
        if (r.ok > max_ok) max_ok = r.ok;
        if (r.max_streak > max_streak) max_streak = r.max_streak;
        if (r.max_ns > worst_call_ns) worst_call_ns = r.max_ns;
        any_failed = any_failed || r.failed;
        std::printf("  writer %2d: ok=%-8llu timeouts=%-8llu max_streak=%-8llu max_call=%llu ns%s\n", i,
            (unsigned long long)r.ok, (unsigned long long)r.timeouts, (unsigned long long)r.max_streak,
            (unsigned long long)r.max_ns, r.failed ? "  (reload failed)" : "");
    }
    if (min_ok == UINT64_MAX) min_ok = 0;

    std::printf("\n== totals ==\n");
    std::printf("  advance_attempts=%llu contentions=%llu retries=%llu advance_timeouts=%llu max_wait_ns=%llu\n",
        (unsigned long long)totals.advance_attempts, (unsigned long long)totals.advance_contentions,
        (unsigned long long)totals.advance_retries, (unsigned long long)totals.advance_timeouts,
        (unsigned long long)totals.advance_max_wait_ns);
    std::printf("  grace_period_timeouts=%llu   samples=%d   peak_backlog=%llu   peak_oldest_age=%llu ns\n",
        (unsigned long long)totals.grace_period_timeouts, samples, (unsigned long long)max_backlog,
        (unsigned long long)max_oldest_age_ns);
    std::printf("  per-writer ok: min=%llu max=%llu total=%llu   longest timeout streak=%llu   worst ok call=%llu ns\n",
        (unsigned long long)min_ok, (unsigned long long)max_ok, (unsigned long long)total_ok,
        (unsigned long long)max_streak, (unsigned long long)worst_call_ns);
    std::printf("  drain after readers stopped: proven=%s  backlog %llu -> %llu\n", drained ? "yes" : "NO",
        (unsigned long long)totals.retired_backlog, (unsigned long long)after_drain.retired_backlog);

    int rc = 0;
    // Starvation needs a comparison: a writer that never succeeded while others
    // clearly did, many times over. "Nobody succeeded" is a shared blocker (a held
    // reader), and a single lucky success is not "others progressed", so both are
    // excluded. `min_timeouts == 0` on a writer that produced no successes would be
    // the strongest form, but it cannot happen here: a timeout is the only way to
    // record a failure.
    const uint64_t progressed = static_cast<uint64_t>(opt.writers) * 64;
    if (!drained) {
        std::printf("  VERDICT: HANDSHAKE NOT DRAINED after readers stopped\n");
        rc = 2;
    } else if (any_failed) {
        std::printf("  VERDICT: a reload failed (plugin missing or unloadable)\n");
        rc = 3;
    } else if (total_ok >= progressed && min_ok == 0) {
        const bool oversubscribed = hw != 0 && static_cast<unsigned>(threads) > hw;
        std::printf(
            "  VERDICT: STARVATION SUSPECTED - a writer never advanced while others advanced %llu times "
            "(grace_period_timeouts=%llu)%s\n",
            (unsigned long long)max_ok, (unsigned long long)totals.grace_period_timeouts,
            oversubscribed ? "; re-run at or below hardware_concurrency before believing it (oversubscribed)" : "");
        rc = 1;
    } else if (totals.grace_period_timeouts > 0 && min_ok < 64) {
        std::printf(
            "  VERDICT: few or no advancements proven (min ok=%llu, total=%llu) with grace_period_timeouts=%llu: a "
            "reader held the grace period open (shared blocker, not writer starvation); backlog drained to %llu "
            "after it left\n",
            (unsigned long long)min_ok, (unsigned long long)total_ok, (unsigned long long)totals.grace_period_timeouts,
            (unsigned long long)after_drain.retired_backlog);
    } else if (total_ok < progressed) {
        std::printf("  VERDICT: too few successes to judge fairness in this window (%llu < %llu)\n",
            (unsigned long long)total_ok, (unsigned long long)progressed);
    } else {
        std::printf("  VERDICT: no starvation observed in this window\n");
    }
    if (capped) std::printf("  NOTE: writers were stopped early at the backlog cap; fairness numbers are truncated\n");
    return rc;
}
