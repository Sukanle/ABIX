// Advancement diagnostics (`SKL_ABIX_RCU_ENABLE_STATS`).
//
// The counters are compiled out of the default build, so this suite is its own
// executable with the flag forced on (see test/CMakeLists.txt). It exists to
// answer one question before any fairness mechanism is designed: does a writer
// actually starve on the advancement right, and can the two failure modes be told
// apart - never got the advancement (`advance_timeouts`) versus got it but a
// reader did not leave (`grace_period_timeouts`)?
#include "test_common.hpp"

#include <atomic>
#include <thread>
#include <vector>

namespace {

int g_reclaimed = 0;

void reclaim_token(void *p) noexcept {
    ++g_reclaimed;
    delete static_cast<int *>(p);
}

// A reader that stays inside the read side until released, so a grace period can
// be made impossible to prove on demand.
class HeldReader {
public:
    explicit HeldReader(skl::abix::rcu::Domain &dom)
        : _dom(dom) {
        _thread = std::thread([this] {
            _dom.enter();
            _inside.store(true, std::memory_order_release);
            while (!_release.load(std::memory_order_acquire))
                std::this_thread::yield();
            _dom.exit();
        });
        while (!_inside.load(std::memory_order_acquire))
            std::this_thread::yield();
    }
    HeldReader(const HeldReader &) = delete;
    HeldReader &operator=(const HeldReader &) = delete;
    ~HeldReader() { release(); }

    void release() noexcept {
        _release.store(true, std::memory_order_release);
        if (_thread.joinable()) _thread.join();
    }

private:
    skl::abix::rcu::Domain &_dom;
    std::thread _thread;
    std::atomic<bool> _inside{false};
    std::atomic<bool> _release{false};
};

}   // namespace

TEST_CASE("47.advance_diagnostics", "[rcu][stats]") {
    using skl::abix::rcu::Deadline;
    using skl::abix::rcu::Domain;
    auto &dom = Domain::instance();

    REQUIRE(Domain::stats_enabled());

    SECTION("47a.an uncontended advance records no wait") {
        dom.reset_stats();
        g_reclaimed = 0;
        const uint64_t token = dom.retire_token(new int(1), reclaim_token);
        REQUIRE(token != 0);

        REQUIRE(dom.synchronize_until(Deadline::after_ms(2'000)));

        const auto s = dom.stats();
        REQUIRE(s.advance_attempts == 1);      // exactly one owner attempt
        REQUIRE(s.advance_contentions == 0);   // nobody else was advancing
        REQUIRE(s.advance_retries == 0);
        REQUIRE(s.advance_timeouts == 0);
        REQUIRE(s.grace_period_timeouts == 0);
        REQUIRE(s.advance_max_wait_ns == 0);   // never had to wait for the right
        REQUIRE(s.retired_backlog == 0);       // the proven grace period collected it
        REQUIRE(g_reclaimed == 1);
        log_info("uncontended: 1 attempt, 0 contention, 0 wait");
    }

    SECTION("47b.a held reader is a grace-period timeout, not an advance timeout") {
        dom.reset_stats();
        HeldReader reader(dom);
        const uint64_t token = dom.retire_token(new int(1), reclaim_token);

        REQUIRE(!dom.synchronize_until(Deadline::after_ms(5)));

        const auto s = dom.stats();
        REQUIRE(s.advance_attempts >= 1);
        REQUIRE(s.advance_timeouts == 0);        // the advancement *was* ours
        REQUIRE(s.grace_period_timeouts >= 1);   // but a reader held it open
        REQUIRE(s.retired_backlog >= 1);         // nothing could be released
        REQUIRE(s.retired_oldest_age_ns > 0);    // and the backlog has an age
        REQUIRE(dom.reclaimed_through(token, skl::abix::rcu::ReclaimClass::batched) == false);

        reader.release();
        REQUIRE(dom.synchronize_until(Deadline::after_ms(2'000)));
        REQUIRE(dom.reclaimed_through(token, skl::abix::rcu::ReclaimClass::batched));
        log_info("held reader: grace_period_timeouts=%llu, advance_timeouts=%llu, backlog age=%llu ns",
            (unsigned long long)s.grace_period_timeouts, (unsigned long long)s.advance_timeouts,
            (unsigned long long)s.retired_oldest_age_ns);
    }

    SECTION("47c.contending writers record contention and advance timeouts") {
        dom.reset_stats();
        HeldReader reader(dom);

        // With the advancement held for a whole grace period, every other writer
        // can only retry until its deadline: that is precisely the starvation
        // signal these counters exist to detect.
        constexpr int kWriters = 4;
        std::vector<std::thread> writers;
        writers.reserve(kWriters);
        for (int i = 0; i < kWriters; ++i)
            writers.emplace_back([&dom] { (void)dom.synchronize_until(Deadline::after_ms(20)); });
        for (auto &w : writers)
            w.join();

        const auto s = dom.stats();
        REQUIRE(s.advance_contentions >= 1);
        REQUIRE(s.advance_timeouts >= 1);
        REQUIRE(s.advance_retries >= 1);
        REQUIRE(s.advance_max_wait_ns > 0);
        REQUIRE(s.advance_attempts >= s.advance_contentions);
        log_info("contention: attempts=%llu, contentions=%llu, retries=%llu, advance_timeouts=%llu, max_wait=%llu ns",
            (unsigned long long)s.advance_attempts, (unsigned long long)s.advance_contentions,
            (unsigned long long)s.advance_retries, (unsigned long long)s.advance_timeouts,
            (unsigned long long)s.advance_max_wait_ns);

        reader.release();
        REQUIRE(dom.synchronize_until(Deadline::after_ms(2'000)));
    }

    SECTION("47d.reset_stats zeroes the counters and the backlog gauge follows the list") {
        dom.reset_stats();
        auto s = dom.stats();
        REQUIRE(s.advance_attempts == 0);
        REQUIRE(s.advance_contentions == 0);
        REQUIRE(s.advance_retries == 0);
        REQUIRE(s.advance_timeouts == 0);
        REQUIRE(s.advance_max_wait_ns == 0);
        REQUIRE(s.grace_period_timeouts == 0);

        // The gauge counts *published* nodes; a few `retire` calls still sit in the
        // thread-local batch, so push past the publish threshold to make it visible.
        for (int i = 0; i < SKL_ABIX_RCU_BATCH_PUBLISH; ++i)
            dom.retire(new int(1), reclaim_token);

        s = dom.stats();
        REQUIRE(s.retired_backlog >= SKL_ABIX_RCU_BATCH_PUBLISH);
        REQUIRE(s.retired_oldest_age_ns > 0);

        // Drain so the singleton is left clean for the other suites.
        REQUIRE(dom.synchronize_until(Deadline::after_ms(2'000)));
        s = dom.stats();
        REQUIRE(s.retired_backlog == 0);
        log_info("backlog gauge: %d published nodes visible, then drained to 0", SKL_ABIX_RCU_BATCH_PUBLISH);
    }
}
