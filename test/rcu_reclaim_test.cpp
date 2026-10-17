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
