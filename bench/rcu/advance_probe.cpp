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
// share, longest timeout streak, backlog trend), not a per-operation cost. A
// single run is an anecdote: --repeat reports the spread across repetitions and
// --sweep walks the writer count from one thread up past the core count.
//
// Repetitions run in *separate processes*. The Domain's reader registry only ever
// grows (there is deliberately no unregister), so repeated runs in one process
// would keep lengthening the lock-free `readers_passed` scan and make each later
// repetition look slower - a harness artifact, not a fairness signal. The parent
// therefore spawns itself once per (level, repetition) and parses a single RESULT
// line from each child.
//
// Development-only: the target is built only when SKL_ABIX_DEVELOPMENT is ON, and
// it forces SKL_ABIX_RCU_ENABLE_STATS because the Domain counters are what
// distinguish the two failure modes a single "timeout" number would conflate:
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
//     --repeat R         repetitions per configuration; >1 reports a distribution (default 1)
//     --sweep [list]     walk writer counts (default: powers of two from 1 up to
//                        the first level at/above hardware_concurrency); an explicit
//                        comma list overrides it, e.g. --sweep 1,2,4,8,16
//     --reload [plugin]  drive the real Object::reload path instead of the raw
//                        Domain (plugin default "reload_dll_a")
//     --run-one          internal: run a single window in this process (used by the
//                        parent when it spawns repetitions) and print the RESULT line
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
// Exit code: 0 no anomaly, 1 suspected starvation, 2 the handshake could not be
// drained after the readers stopped, 3 a reload failed, 64 a bad invocation or a
// child that produced no result. A run where *nobody* advances because a reader is
// held is not starvation: it is a shared blocker.
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

#if defined(_WIN32)
#  define ABIX_POPEN _popen
#  define ABIX_PCLOSE _pclose
#else
#  define ABIX_POPEN popen
#  define ABIX_PCLOSE pclose
#endif

namespace {

using skl::abix::rcu::Deadline;
using skl::abix::rcu::Domain;

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
    int repeat = 1;
    bool reload = false;
    bool sweep = false;
    bool run_one = false;   // internal, set by the parent when it spawns a repetition
    std::vector<int> sweep_list;
    std::string plugin = "reload_dll_a";
};

struct WriterReport {
    uint64_t ok = 0;
    uint64_t timeouts = 0;
    uint64_t max_streak = 0;   // longest run of consecutive timeouts
    uint64_t max_ns = 0;       // longest single call that succeeded
    bool failed = false;       // reload mode: load/reload returned false
};

// One run of the window, fully drained at the end.
struct RunResult {
    int writers = 0;
    std::vector<WriterReport> reports;
    Domain::Stats totals{};
    Domain::Stats after_drain{};
    uint64_t peak_backlog = 0;
    uint64_t peak_oldest_age_ns = 0;
    uint64_t total_ok = 0;
    uint64_t min_ok = 0;
    uint64_t max_ok = 0;
    uint64_t max_streak = 0;
    uint64_t worst_call_ns = 0;
    bool drained = false;
    bool capped = false;
    bool any_failed = false;
    int verdict = 0;   // 0 clean, 1 starvation, 2 not drained, 3 reload failed
};

Deadline make_deadline(uint64_t ms) { return Deadline::after_ms(ms); }

void reader_loop(Domain &dom, std::atomic<bool> &running, uint64_t hold_ns) {
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
void raw_writer_loop(
    Domain &dom, Deadline deadline, std::atomic<bool> &running, uint64_t retire_per_call, WriterReport &rep) {
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

RunResult run_once(const Options &opt, int writers, unsigned hw, bool verbose) {
    Domain &dom = Domain::instance();
    const Deadline deadline = make_deadline(opt.deadline_ms);
    const uint64_t hold_ns = opt.hold_us * 1'000ULL;
    const std::string path = opt.reload ? dll_path(opt.plugin.c_str()) : std::string{};

    RunResult r;
    r.writers = writers;

    dom.reset_stats();

    std::atomic<bool> running{true};
    r.reports.assign(static_cast<size_t>(writers), WriterReport{});
    std::vector<std::thread> workers;

    for (int i = 0; i < opt.readers; ++i)
        workers.emplace_back(reader_loop, std::ref(dom), std::ref(running), hold_ns);

    for (int i = 0; i < writers; ++i) {
        if (opt.reload)
            workers.emplace_back(reload_writer_loop, std::ref(running), opt.deadline_ms, std::cref(path),
                std::ref(r.reports[(size_t)i]));
        else
            workers.emplace_back(raw_writer_loop, std::ref(dom), deadline, std::ref(running), opt.retire_per_call,
                std::ref(r.reports[(size_t)i]));
    }

    // Sample the domain while the window is open: the backlog trend is the
    // "retired queue accumulating?" half of the question. The cap is polled more
    // often than the report line is printed, because a held reader grows the
    // backlog unbounded by design and the probe should not exhaust memory.
    const uint64_t window_start = now_ns();
    const uint64_t window_ns = opt.duration_ms * 1'000'000ULL;
    const uint64_t poll_ns = 50'000'000ULL;   // 50 ms: the cap check cadence
    uint64_t last_report_ns = 0;
    while (now_ns() - window_start < window_ns) {
        const uint64_t elapsed = now_ns() - window_start;
        const uint64_t remaining = window_ns - elapsed;
        std::this_thread::sleep_for(std::chrono::microseconds(std::min<uint64_t>(poll_ns, remaining) / 1'000ULL));

        const auto s = dom.stats();
        if (s.retired_backlog > r.peak_backlog) r.peak_backlog = s.retired_backlog;
        if (s.retired_oldest_age_ns > r.peak_oldest_age_ns) r.peak_oldest_age_ns = s.retired_oldest_age_ns;

        if (opt.max_backlog != 0 && s.retired_backlog >= opt.max_backlog) {
            r.capped = true;
            running.store(false, std::memory_order_relaxed);
            std::printf("  backlog cap reached (%llu >= %llu): stopping writers early\n",
                (unsigned long long)s.retired_backlog, (unsigned long long)opt.max_backlog);
            break;
        }

        if (verbose && now_ns() - window_start - last_report_ns >= opt.sample_ms * 1'000'000ULL) {
            last_report_ns = now_ns() - window_start;
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

    r.totals = dom.stats();

    // Drain: with every reader stopped, one bounded advancement must prove and
    // collect. A failure here is a wedged handshake, not a fairness problem.
    r.drained = dom.synchronize_until(Deadline::after_ms(5'000));
    r.after_drain = dom.stats();

    r.min_ok = UINT64_MAX;
    for (const auto &rep : r.reports) {
        r.total_ok += rep.ok;
        if (rep.ok < r.min_ok) r.min_ok = rep.ok;
        if (rep.ok > r.max_ok) r.max_ok = rep.ok;
        if (rep.max_streak > r.max_streak) r.max_streak = rep.max_streak;
        if (rep.max_ns > r.worst_call_ns) r.worst_call_ns = rep.max_ns;
        r.any_failed = r.any_failed || rep.failed;
    }
    if (r.min_ok == UINT64_MAX) r.min_ok = 0;

    // Starvation needs a comparison: a writer that never succeeded while others
    // clearly did, many times over. "Nobody succeeded" is a shared blocker (a held
    // reader), and a single lucky success is not "others progressed".
    const uint64_t progressed = static_cast<uint64_t>(writers) * 64;
    if (!r.drained) {
        r.verdict = 2;
    } else if (r.any_failed) {
        r.verdict = 3;
    } else if (r.total_ok >= progressed && r.min_ok == 0) {
        r.verdict = 1;
    }
    (void)hw;
    return r;
}

// ── reporting ────────────────────────────────────────────────

void print_verdict(const Options &opt, unsigned hw, const RunResult &r) {
    const int threads = r.writers + opt.readers;
    switch (r.verdict) {
        case 2: std::printf("  VERDICT: HANDSHAKE NOT DRAINED after readers stopped\n"); break;
        case 3: std::printf("  VERDICT: a reload failed (plugin missing or unloadable)\n"); break;
        case 1: {
            const bool oversubscribed = hw != 0 && static_cast<unsigned>(threads) > hw;
            std::printf(
                "  VERDICT: STARVATION SUSPECTED - a writer never advanced while others advanced %llu times "
                "(grace_period_timeouts=%llu)%s\n",
                (unsigned long long)r.max_ok, (unsigned long long)r.totals.grace_period_timeouts,
                oversubscribed ? "; re-run at or below hardware_concurrency before believing it (oversubscribed)" : "");
            break;
        }
        default:
            if (r.totals.grace_period_timeouts > 0 && r.min_ok < 64) {
                std::printf(
                    "  VERDICT: few or no advancements proven (min ok=%llu, total=%llu) with "
                    "grace_period_timeouts=%llu: a reader held the grace period open (shared blocker, not "
                    "writer starvation); backlog drained to %llu after it left\n",
                    (unsigned long long)r.min_ok, (unsigned long long)r.total_ok,
                    (unsigned long long)r.totals.grace_period_timeouts,
                    (unsigned long long)r.after_drain.retired_backlog);
            } else if (r.total_ok < static_cast<uint64_t>(r.writers) * 64) {
                std::printf("  VERDICT: too few successes to judge fairness in this window\n");
            } else {
                std::printf("  VERDICT: no starvation observed in this window\n");
            }
            break;
    }
    if (r.capped)
        std::printf("  NOTE: writers were stopped early at the backlog cap; fairness numbers are truncated\n");
}

void print_verbose_run(const Options &opt, unsigned hw, const RunResult &r) {
    std::printf("\n== per writer (writers=%d) ==\n", r.writers);
    for (size_t i = 0; i < r.reports.size(); ++i) {
        const auto &rep = r.reports[i];
        std::printf("  writer %2zu: ok=%-8llu timeouts=%-8llu max_streak=%-8llu max_call=%llu ns%s\n", i,
            (unsigned long long)rep.ok, (unsigned long long)rep.timeouts, (unsigned long long)rep.max_streak,
            (unsigned long long)rep.max_ns, rep.failed ? "  (reload failed)" : "");
    }
    std::printf("\n== totals ==\n");
    std::printf("  advance_attempts=%llu contentions=%llu retries=%llu advance_timeouts=%llu max_wait_ns=%llu\n",
        (unsigned long long)r.totals.advance_attempts, (unsigned long long)r.totals.advance_contentions,
        (unsigned long long)r.totals.advance_retries, (unsigned long long)r.totals.advance_timeouts,
        (unsigned long long)r.totals.advance_max_wait_ns);
    std::printf("  grace_period_timeouts=%llu   peak_backlog=%llu   peak_oldest_age=%llu ns\n",
        (unsigned long long)r.totals.grace_period_timeouts, (unsigned long long)r.peak_backlog,
        (unsigned long long)r.peak_oldest_age_ns);
    std::printf("  per-writer ok: min=%llu max=%llu total=%llu   longest timeout streak=%llu   worst ok call=%llu ns\n",
        (unsigned long long)r.min_ok, (unsigned long long)r.max_ok, (unsigned long long)r.total_ok,
        (unsigned long long)r.max_streak, (unsigned long long)r.worst_call_ns);
    std::printf("  drain after readers stopped: proven=%s  backlog %llu -> %llu\n", r.drained ? "yes" : "NO",
        (unsigned long long)r.totals.retired_backlog, (unsigned long long)r.after_drain.retired_backlog);
    print_verdict(opt, hw, r);
}

// The only line the parent parses. Keep the key names in sync with parse_result_line.
void print_result_line(const RunResult &r) {
    std::printf(
        "RESULT writers=%d min_ok=%llu max_ok=%llu total_ok=%llu streak=%llu adv_to=%llu grace_to=%llu "
        "backlog=%llu drained=%d capped=%d verdict=%d\n",
        r.writers, (unsigned long long)r.min_ok, (unsigned long long)r.max_ok, (unsigned long long)r.total_ok,
        (unsigned long long)r.max_streak, (unsigned long long)r.totals.advance_timeouts,
        (unsigned long long)r.totals.grace_period_timeouts, (unsigned long long)r.peak_backlog, r.drained ? 1 : 0,
        r.capped ? 1 : 0, r.verdict);
}

bool parse_result_line(const char *text, RunResult &r) {
    bool saw_min = false;
    std::string s(text);
    size_t pos = 0;
    while (pos < s.size()) {
        while (pos < s.size() && s[pos] == ' ')
            ++pos;
        const size_t eq = s.find('=', pos);
        if (eq == std::string::npos) break;
        const size_t end = s.find(' ', eq);
        const std::string key = s.substr(pos, eq - pos);
        const std::string val = s.substr(eq + 1, end == std::string::npos ? std::string::npos : end - eq - 1);
        const unsigned long long v = std::strtoull(val.c_str(), nullptr, 10);
        if (key == "writers")
            r.writers = static_cast<int>(v);
        else if (key == "min_ok") {
            r.min_ok = v;
            saw_min = true;
        } else if (key == "max_ok")
            r.max_ok = v;
        else if (key == "total_ok")
            r.total_ok = v;
        else if (key == "streak")
            r.max_streak = v;
        else if (key == "adv_to")
            r.totals.advance_timeouts = v;
        else if (key == "grace_to")
            r.totals.grace_period_timeouts = v;
        else if (key == "backlog")
            r.peak_backlog = v;
        else if (key == "drained")
            r.drained = (v != 0);
        else if (key == "capped")
            r.capped = (v != 0);
        else if (key == "verdict")
            r.verdict = static_cast<int>(v);
        pos = (end == std::string::npos) ? s.size() : end + 1;
    }
    return saw_min;
}

// One fresh process per repetition: no reader-registry growth across windows and a
// clean scheduler state.
bool run_child(const char *exe, const Options &opt, int writers, RunResult &out, std::string &child_output) {
    std::string cmd = "\"";
    cmd += exe;
    cmd += "\" --run-one";
    cmd += " --writers " + std::to_string(writers);
    cmd += " --readers " + std::to_string(opt.readers);
    cmd += " --hold-us " + std::to_string(opt.hold_us);
    cmd += " --deadline-ms " + std::to_string(opt.deadline_ms);
    cmd += " --duration-ms " + std::to_string(opt.duration_ms);
    cmd += " --retire-per-call " + std::to_string(opt.retire_per_call);
    cmd += " --max-backlog " + std::to_string(opt.max_backlog);
    if (opt.reload) {
        cmd += " --reload ";
        cmd += opt.plugin;
    }

    FILE *pipe = ABIX_POPEN(cmd.c_str(), "r");
    if (!pipe) return false;

    char line[2'048];
    bool got = false;
    while (std::fgets(line, sizeof(line), pipe)) {
        child_output += line;
        if (std::strncmp(line, "RESULT ", 7) == 0) got = parse_result_line(line + 7, out);
    }
    ABIX_PCLOSE(pipe);
    out.writers = writers;
    return got;
}

template<typename T>
T pick(const std::vector<T> &sorted, double q) {
    if (sorted.empty()) return T{};
    size_t idx = static_cast<size_t>(q * (double)(sorted.size() - 1) + 0.5);
    if (idx >= sorted.size()) idx = sorted.size() - 1;
    return sorted[idx];
}

void print_metric_row(const char *name, std::vector<uint64_t> values) {
    std::sort(values.begin(), values.end());
    std::printf("  %-22s %-14llu %-14llu %-14llu\n", name, (unsigned long long)pick(values, 0.0),
        (unsigned long long)pick(values, 0.5), (unsigned long long)pick(values, 1.0));
}

void print_distribution(const Options &opt, unsigned hw, const std::vector<RunResult> &runs) {
    std::vector<uint64_t> min_ok, max_ok, streak, adv_to, grace_to, backlog;
    uint64_t starved = 0, not_drained = 0, reload_failed = 0, capped = 0;
    for (const auto &r : runs) {
        min_ok.push_back(r.min_ok);
        max_ok.push_back(r.max_ok);
        streak.push_back(r.max_streak);
        adv_to.push_back(r.totals.advance_timeouts);
        grace_to.push_back(r.totals.grace_period_timeouts);
        backlog.push_back(r.peak_backlog);
        if (r.verdict == 1) ++starved;
        if (r.verdict == 2) ++not_drained;
        if (r.verdict == 3) ++reload_failed;
        if (r.capped) ++capped;
    }
    const bool oversubscribed = hw != 0 && static_cast<unsigned>(opt.writers + opt.readers) > hw;
    std::printf("\n== distribution over %zu separate processes (writers=%d deadline=%llums mode=%s) ==\n", runs.size(),
        opt.writers, (unsigned long long)opt.deadline_ms, opt.reload ? "reload" : "raw");
    if (oversubscribed)
        std::printf("  OVERSUBSCRIBED (%d threads > %u hw): skew may reflect the OS scheduler, not the protocol\n",
            opt.writers + opt.readers, hw);
    std::printf("  %-22s %-14s %-14s %-14s\n", "metric", "min", "median", "max");
    print_metric_row("per-writer min ok", min_ok);
    print_metric_row("per-writer max ok", max_ok);
    print_metric_row("longest streak", streak);
    print_metric_row("advance_timeouts", adv_to);
    print_metric_row("grace_period_timeouts", grace_to);
    print_metric_row("peak_backlog", backlog);
    std::printf("  verdicts: starvation=%llu not_drained=%llu reload_failed=%llu clean=%zu",
        (unsigned long long)starved, (unsigned long long)not_drained, (unsigned long long)reload_failed,
        runs.size() - (size_t)starved - (size_t)not_drained - (size_t)reload_failed);
    if (capped) std::printf(" (backlog-capped=%llu)", (unsigned long long)capped);
    std::printf("\n");
}

void print_sweep(const Options &opt, unsigned hw, const std::vector<int> &levels,
    const std::vector<std::vector<RunResult>> &per_level) {
    std::printf("\n== sweep (deadline=%llums, %d process(es) per level, mode=%s, hw=%u) ==\n",
        (unsigned long long)opt.deadline_ms, opt.repeat, opt.reload ? "reload" : "raw", hw);
    std::printf("  %-8s %-8s %-13s %-11s %-11s %-12s %-12s %-9s %-9s %s\n", "writers", "reps", "min_ok(med)",
        "ratio(med)", "ratio(max)", "streak(med)", "streak(max)", "starved", "capped", "backlog(max)");
    bool any_oversubscribed = false;
    for (size_t i = 0; i < levels.size(); ++i) {
        const auto &runs = per_level[i];
        std::vector<uint64_t> min_ok, streak;
        std::vector<double> ratio;
        uint64_t starved = 0, capped = 0, backlog_max = 0;
        const bool oversubscribed = hw != 0 && static_cast<unsigned>(levels[i] + opt.readers) > hw;
        any_oversubscribed = any_oversubscribed || oversubscribed;
        char writers_cell[32];
        std::snprintf(writers_cell, sizeof(writers_cell), "%d%s", levels[i], oversubscribed ? "*" : "");
        for (const auto &r : runs) {
            min_ok.push_back(r.min_ok);
            streak.push_back(r.max_streak);
            ratio.push_back(r.min_ok == 0 ? 0.0 : (double)r.max_ok / (double)r.min_ok);
            if (r.verdict == 1) ++starved;
            if (r.capped) ++capped;
            if (r.peak_backlog > backlog_max) backlog_max = r.peak_backlog;
        }
        std::sort(ratio.begin(), ratio.end());
        std::sort(min_ok.begin(), min_ok.end());
        std::sort(streak.begin(), streak.end());

        bool any_zero = false;
        for (uint64_t m : min_ok)
            any_zero = any_zero || m == 0;
        char med_buf[32];
        char max_buf[32];
        if (any_zero) {
            // At least one process had a writer that never advanced: the ratio is
            // unbounded, which is the strongest thing this table can show.
            std::snprintf(med_buf, sizeof(med_buf), "inf");
            std::snprintf(max_buf, sizeof(max_buf), "inf");
        } else {
            std::snprintf(med_buf, sizeof(med_buf), "%.2f", pick(ratio, 0.5));
            std::snprintf(max_buf, sizeof(max_buf), "%.2f", pick(ratio, 1.0));
        }

        std::printf("  %-8s %-8zu %-13llu %-11s %-11s %-12llu %-12llu %-9llu %-9llu %llu\n", writers_cell, runs.size(),
            (unsigned long long)pick(min_ok, 0.5), med_buf, max_buf, (unsigned long long)pick(streak, 0.5),
            (unsigned long long)pick(streak, 1.0), (unsigned long long)starved, (unsigned long long)capped,
            (unsigned long long)backlog_max);
    }
    std::printf("  ratio = per-writer max ok / min ok (1.00 is perfectly even); inf means a writer never advanced\n");
    if (any_oversubscribed)
        std::printf(
            "  * writers + readers > hardware_concurrency (oversubscribed): skew may reflect the OS scheduler\n");
}

std::vector<int> default_sweep_list(unsigned hw) {
    std::vector<int> v;
    const int cap = hw ? static_cast<int>(hw) : 8;
    int w = 1;
    while (w < cap) {
        v.push_back(w);
        w <<= 1;
    }
    v.push_back(w);   // first level at or above the core count
    return v;
}

void parse_int_list(const char *text, std::vector<int> &out) {
    out.clear();
    const char *p = text;
    while (*p) {
        char *end = nullptr;
        const long v = std::strtol(p, &end, 10);
        if (end == p) break;
        if (v > 0) out.push_back(static_cast<int>(v));
        p = end;
        while (*p == ',' || *p == ' ')
            ++p;
    }
}

void print_help() {
    std::printf(
        "rcu_advance_probe [options]\n"
        "  --writers N          competing writer threads            (default 4)\n"
        "  --readers N          reader threads entering/exiting RCU  (default 0)\n"
        "  --hold-us U          reader stays inside per visit, us    (default 0)\n"
        "  --deadline-ms M      per-call wait bound, 0 = non-blocking (default 1)\n"
        "  --duration-ms D      wall-clock window per repetition     (default 2000)\n"
        "  --sample-ms T        stats printing period                (default 200)\n"
        "  --retire-per-call K  nodes retired per raw iteration      (default 1)\n"
        "  --max-backlog K      stop writers at K published nodes    (default 250000)\n"
        "  --repeat R           repetitions in separate processes    (default 1)\n"
        "  --sweep [list]       walk writer counts, e.g. 1,2,4,8,16  (default: powers of two)\n"
        "  --reload [plugin]    drive Object::reload (default reload_dll_a)\n"
        "  --help\n");
}

int worst_rc(const std::vector<int> &verdicts) {
    int rc = 0;
    for (int v : verdicts) {
        if (v == 3) return 3;
        if (v == 2 && rc < 2) rc = 2;
        if (v == 1 && rc < 1) rc = 1;
    }
    return rc;
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
        } else if (std::strcmp(a, "--repeat") == 0 && has_next) {
            opt.repeat = std::atoi(argv[++i]);
        } else if (std::strcmp(a, "--sweep") == 0) {
            opt.sweep = true;
            if (has_next && argv[i + 1][0] != '-') parse_int_list(argv[++i], opt.sweep_list);
        } else if (std::strcmp(a, "--reload") == 0) {
            opt.reload = true;
            if (has_next && argv[i + 1][0] != '-') opt.plugin = argv[++i];
        } else if (std::strcmp(a, "--run-one") == 0) {
            opt.run_one = true;
        } else {
            std::fprintf(stderr, "unknown option: %s (try --help)\n", a);
            return 64;
        }
    }
    if (opt.writers <= 0) {
        std::fprintf(stderr, "--writers must be >= 1\n");
        return 64;
    }
    if (opt.repeat <= 0) opt.repeat = 1;
    if (opt.sample_ms == 0) opt.sample_ms = 1;
    if (opt.reload && opt.deadline_ms == 0) {
        // For Object a 0 timeout means *unbounded*, not a non-blocking probe, so a
        // held reader would block the reload forever and the probe could not stop.
        std::fprintf(stderr, "--reload needs --deadline-ms >= 1 (0 means unbounded for Object)\n");
        return 64;
    }
    if (!Domain::stats_enabled()) {
        std::fprintf(stderr, "this binary was built without SKL_ABIX_RCU_ENABLE_STATS\n");
        return 64;
    }

    const unsigned hw = std::thread::hardware_concurrency();
    const int threads = opt.writers + opt.readers;

    // Child process: one window, one RESULT line. The parent runs this.
    if (opt.run_one) {
        std::printf("rcu_advance_probe --run-one (writers=%d mode=%s deadline=%llums)\n", opt.writers,
            opt.reload ? "reload" : "raw", (unsigned long long)opt.deadline_ms);
        RunResult r = run_once(opt, opt.writers, hw, /*verbose=*/true);
        print_result_line(r);
        return r.verdict;
    }

    std::printf("rcu_advance_probe\n");
    std::printf("  mode=%s readers=%d hold_us=%llu deadline_ms=%llu duration_ms=%llu retire_per_call=%llu repeat=%d\n",
        opt.reload ? "reload" : "raw", opt.readers, (unsigned long long)opt.hold_us,
        (unsigned long long)opt.deadline_ms, (unsigned long long)opt.duration_ms,
        (unsigned long long)opt.retire_per_call, opt.repeat);
    std::printf("  hardware_concurrency=%u\n", hw);
    if (opt.reload) std::printf("  plugin=%s\n", dll_path(opt.plugin.c_str()).c_str());

    const bool orchestrate = opt.sweep || opt.repeat > 1;

    if (!orchestrate) {
        // Single interactive run.
        if (hw != 0 && static_cast<unsigned>(threads) > hw) {
            std::printf(
                "  WARNING: %d threads > %u hw threads (oversubscribed): success spread and timeout streaks may "
                "reflect OS scheduling, not the advancement protocol\n",
                threads, hw);
        }
        RunResult r = run_once(opt, opt.writers, hw, /*verbose=*/true);
        print_verbose_run(opt, hw, r);
        return r.verdict;
    }

    // Orchestrated: one fresh process per (level, repetition).
    const std::vector<int> levels =
        opt.sweep ? (opt.sweep_list.empty() ? default_sweep_list(hw) : opt.sweep_list) : std::vector<int>{opt.writers};
    std::vector<std::vector<RunResult>> per_level;
    std::vector<int> verdicts;
    for (int w : levels) {
        std::printf("\n-- writers=%d --\n", w);
        std::vector<RunResult> runs;
        for (int rep = 0; rep < opt.repeat; ++rep) {
            RunResult r;
            std::string child_output;
            if (!run_child(argv[0], opt, w, r, child_output)) {
                std::fprintf(stderr, "child for writers=%d rep=%d produced no RESULT line:\n%s", w, rep + 1,
                    child_output.c_str());
                return 64;
            }
            std::printf(
                "  rep %d/%d: min_ok=%-10llu max_ok=%-10llu streak=%-8llu adv_to=%-8llu grace_to=%-8llu "
                "backlog=%-8llu verdict=%d\n",
                rep + 1, opt.repeat, (unsigned long long)r.min_ok, (unsigned long long)r.max_ok,
                (unsigned long long)r.max_streak, (unsigned long long)r.totals.advance_timeouts,
                (unsigned long long)r.totals.grace_period_timeouts, (unsigned long long)r.peak_backlog, r.verdict);
            verdicts.push_back(r.verdict);
            runs.push_back(std::move(r));
        }
        per_level.push_back(std::move(runs));
    }

    if (opt.sweep)
        print_sweep(opt, hw, levels, per_level);
    else
        print_distribution(opt, hw, per_level.front());

    const int rc = worst_rc(verdicts);
    std::printf("\nrcu_advance_probe overall: %s\n", rc == 0   ? "no anomaly"
                                                     : rc == 1 ? "starvation suspected in at least one process"
                                                     : rc == 2 ? "a handshake failed to drain"
                                                               : "a reload failed");
    return rc;
}
