#include "test_common.hpp"

namespace {
int g_batched_reclaimed = 0;
int g_eager_reclaimed = 0;

void reclaim_batched_token(void *p) noexcept {
    ++g_batched_reclaimed;
    delete static_cast<int *>(p);
}

void reclaim_eager_token(void *p) noexcept {
    ++g_eager_reclaimed;
    delete static_cast<int *>(p);
}
}   // namespace

// `Domain::retire` defers to the batched epoch advancement (every
// SKL_ABIX_RCU_EPOCH_BATCH `synchronize()` calls), which is what keeps a
// swapped-out module's image - and therefore its mapping - alive for a while.
// `Domain::retire_eager` is reclaimed by the next `synchronize_eager()`, which is
// what lets the very next `build` reuse a retired index slot array. The two
// classes must not interfere in either direction, or one of the two behaviours
// silently disappears.
TEST_CASE("44.reclaim_classes_batched_and_eager", "[rcu][reclaim]") {
    auto &dom = skl::abix::rcu::Domain::instance();

    SECTION("44a.an eager node is reclaimed by the next eager synchronize") {
        g_eager_reclaimed = 0;
        dom.retire_eager(new int(1), reclaim_eager_token);
        dom.synchronize_eager();
        REQUIRE(g_eager_reclaimed == 1);
        log_info("retire_eager + synchronize_eager reclaimed 1 node");
    }

    SECTION("44b.eager rounds do not shorten the batched deferral") {
        g_batched_reclaimed = 0;
        dom.retire(new int(1), reclaim_batched_token);

        // This is the property that keeps an alternating hot reload on the loader's
        // cheap "already resident" path: the retired image survives eager rounds,
        // which now happen on every reload.
        for (int i = 0; i < 4 * SKL_ABIX_RCU_EPOCH_BATCH; ++i)
            dom.synchronize_eager();
        REQUIRE(g_batched_reclaimed == 0);

        for (int i = 0; i < SKL_ABIX_RCU_EPOCH_BATCH; ++i)
            dom.synchronize();
        REQUIRE(g_batched_reclaimed == 1);
        log_info("batched node survived %d eager rounds, then the batched cadence collected it",
            4 * SKL_ABIX_RCU_EPOCH_BATCH);
    }

    SECTION("44c.an eager node is not collected by the batched cadence") {
        g_eager_reclaimed = 0;
        dom.retire_eager(new int(1), reclaim_eager_token);

        for (int i = 0; i < SKL_ABIX_RCU_EPOCH_BATCH; ++i)
            dom.synchronize();
        REQUIRE(g_eager_reclaimed == 0);

        dom.synchronize_eager();
        REQUIRE(g_eager_reclaimed == 1);
        log_info(
            "eager node ignored %d batched synchronize calls, then was reclaimed eagerly", SKL_ABIX_RCU_EPOCH_BATCH);
    }
}

namespace {

// A reader that stays inside the read side until it is released. Several cases below
// need "the grace period cannot be proven", which requires a registered thread that
// holds an epoch old enough to block `readers_passed`.
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

// `synchronize_until()` answers exactly one question - was the requested grace period
// proven within the deadline - and a false answer must not be mistaken for progress:
// no watermark moves, no callback runs, and the domain stays usable afterwards.
TEST_CASE("45.synchronize_until_bounds_only_the_wait", "[rcu][reclaim][deadline]") {
    using skl::abix::rcu::Deadline;
    using skl::abix::rcu::ReclaimClass;
    auto &dom = skl::abix::rcu::Domain::instance();

    SECTION("45a.deadline shapes") {
        REQUIRE(!Deadline::never().bounded());
        REQUIRE(!Deadline::never().expired());
        REQUIRE(Deadline::after_ms(0).bounded());
        REQUIRE(Deadline::after_ms(0).expired());   // a non-blocking probe
        REQUIRE(!Deadline::after_ms(1'000).expired());
        log_info("Deadline: never() unbounded, after_ms(0) already expired");
    }

    SECTION("45b.no reader: proven, and the token is reclaimed") {
        g_batched_reclaimed = 0;
        const uint64_t token = dom.retire_token(new int(1), reclaim_batched_token);
        REQUIRE(token != 0);
        REQUIRE(!dom.reclaimed_through(token, ReclaimClass::batched));

        REQUIRE(dom.synchronize_until(Deadline::after_ms(50)));
        REQUIRE(dom.reclaimed_through(token, ReclaimClass::batched));
        REQUIRE(g_batched_reclaimed >= 1);
        log_info("no reader: grace period proven, node reclaimed");
    }

    SECTION("45c.a held reader times out: nothing reclaimed, epoch already advanced") {
        g_batched_reclaimed = 0;
        HeldReader reader(dom);
        const uint64_t token = dom.retire_token(new int(1), reclaim_batched_token);

        REQUIRE(!dom.synchronize_until(Deadline::after_ms(5)));   // not proven
        REQUIRE(!dom.reclaimed_through(token, ReclaimClass::batched));
        REQUIRE(g_batched_reclaimed == 0);   // no callback ran

        // The advancement is irreversible, so the reader now holds an epoch older than
        // the one the timed-out call published. A later call must still be able to make
        // progress - this is the intermediate state the contract has to survive.
        REQUIRE(!dom.synchronize_until(Deadline::after_ms(5)));
        REQUIRE(!dom.reclaimed_through(token, ReclaimClass::batched));

        reader.release();
        REQUIRE(dom.synchronize_until(Deadline::after_ms(2'000)));
        REQUIRE(dom.reclaimed_through(token, ReclaimClass::batched));
        REQUIRE(g_batched_reclaimed == 1);
        log_info("timeout after the epoch advanced: node stayed pending, later call reclaimed it");
    }

    SECTION("45d.a timed-out call does not leave the handshake stuck") {
        HeldReader reader(dom);
        const uint64_t token = dom.retire_token(new int(1), reclaim_batched_token);
        for (int i = 0; i < 8; ++i)
            REQUIRE(!dom.synchronize_until(Deadline::after_ms(1)));
        reader.release();

        // If the timed-out calls had left `sync` set, this would time out (or spin)
        // instead of returning true.
        REQUIRE(dom.synchronize_until(Deadline::after_ms(2'000)));
        REQUIRE(dom.reclaimed_through(token, ReclaimClass::batched));
        log_info("8 timed-out waits in a row, then a proven one: handshake not stuck");
    }

    SECTION("45e.retire_token(0) is never claimed as reclaimed") {
        REQUIRE(!dom.reclaimed_through(0, ReclaimClass::batched));
        REQUIRE(!dom.reclaimed_through(0, ReclaimClass::eager));
    }
}

// The advancement diagnostics are opt-in. The default build must answer `stats()`
// with an all-zero snapshot and a no-op `reset_stats()`, without taking the writer
// lock. Guarded by the macro so the same file also compiles with diagnostics on
// (`test_all` unity build, or a `-DSKL_ABIX_RCU_ENABLE_STATS=1` build), where this
// case simply does not apply; the wiring itself is covered by `rcu_stats_test`.
#if !SKL_ABIX_RCU_ENABLE_STATS
TEST_CASE("47.advance_diagnostics_are_compiled_out_by_default", "[rcu][reclaim][stats]") {
    auto &dom = skl::abix::rcu::Domain::instance();
    REQUIRE(!skl::abix::rcu::Domain::stats_enabled());

    const auto s = dom.stats();
    REQUIRE(s.advance_attempts == 0);
    REQUIRE(s.advance_contentions == 0);
    REQUIRE(s.advance_retries == 0);
    REQUIRE(s.advance_timeouts == 0);
    REQUIRE(s.advance_max_wait_ns == 0);
    REQUIRE(s.grace_period_timeouts == 0);
    REQUIRE(s.retired_backlog == 0);
    REQUIRE(s.retired_oldest_age_ns == 0);

    dom.reset_stats();   // must be a no-op, not an error
    log_info("diagnostics compiled out: stats() is an all-zero no-op");
}
#endif
