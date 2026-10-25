#include "SKL/ABIX/Util/Log.h"

#include <catch2/catch_test_macros.hpp>
#include <type_traits>

static_assert(std::is_same_v<skl::abix::util::LogState, skl::abix::util::SingleThreadedLogState>,
    "ABIX_LOG_THREAD_SAFE=0 must select the plain-storage policy");

TEST_CASE("24.logging_single_thread_policy", "[log][policy]") {
    using namespace skl::abix::util;

    static int calls = 0;
    LogState state;

    set_log_level(state, LogLevel::Debug);
    set_log_sink(state, [](LogLevel, const char *) { ++calls; });

    log(state, LogLevel::Info, "single-threaded %d", 1);
    ABIX_LOG_INFO_TO(state, "single-threaded macro");
    REQUIRE(calls == 2);

    set_log_level(state, LogLevel::Off);
    log(state, LogLevel::Error, "suppressed");
    REQUIRE(calls == 2);
}
