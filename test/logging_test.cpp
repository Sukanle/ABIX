#include "test_common.hpp"

#include <atomic>
#include <string.h>
#include <string>
#include <thread>
#include <vector>

TEST_CASE("19.logging_basic", "[log][prompt19]") {
    log_info("Test 19: basic logging — verify log levels, runtime filtering, set_log_sink, and ABIX_LOG_* macros");

    const auto previous_sink = skl::abix::util::get_log_sink();
    const auto previous_level = skl::abix::util::get_log_level();

    static int log_count = 0;
    static auto last_level = skl::abix::util::LogLevel::Debug;
    static char last_msg[256] = {};

    auto sink = [](skl::abix::util::LogLevel level, const char *msg) {
        ++log_count;
        last_level = level;
        snprintf(last_msg, sizeof(last_msg), "%s", msg);
    };

    skl::abix::util::set_log_level(skl::abix::util::LogLevel::Debug);
    skl::abix::util::set_log_sink(sink);
    log_count = 0;

    ABIX_LOG_DEBUG("debug message %d", 1);
    REQUIRE(log_count == 1);
    REQUIRE(last_level == skl::abix::util::LogLevel::Debug);
    REQUIRE(strstr(last_msg, "debug message 1") != nullptr);

    ABIX_LOG_INFO("info message %d", 2);
    REQUIRE(log_count == 2);
    REQUIRE(last_level == skl::abix::util::LogLevel::Info);
    REQUIRE(strstr(last_msg, "info message 2") != nullptr);

    ABIX_LOG_WARNING("warning message %d", 3);
    REQUIRE(log_count == 3);
    REQUIRE(last_level == skl::abix::util::LogLevel::Warning);
    REQUIRE(strstr(last_msg, "warning message 3") != nullptr);

    ABIX_LOG_ERROR("error message %d", 4);
    REQUIRE(log_count == 4);
    REQUIRE(last_level == skl::abix::util::LogLevel::Error);
    REQUIRE(strstr(last_msg, "error message 4") != nullptr);

    // Runtime threshold: Warning suppresses Debug/Info but not Warning/Error.
    skl::abix::util::set_log_level(skl::abix::util::LogLevel::Warning);
    log_count = 0;
    ABIX_LOG_DEBUG("suppressed debug");
    ABIX_LOG_INFO("suppressed info");
    REQUIRE(log_count == 0);
    ABIX_LOG_WARNING("visible warning");
    ABIX_LOG_ERROR("visible error");
    REQUIRE(log_count == 2);

    // Off disables logs without changing error-return semantics.
    skl::abix::util::set_log_level(skl::abix::util::LogLevel::Off);
    ABIX_LOG_ERROR("suppressed error");
    REQUIRE(log_count == 2);

    skl::abix::util::set_log_sink(nullptr);
    log_count = 0;
    ABIX_LOG_ERROR("no sink");
    REQUIRE(log_count == 0);

    skl::abix::util::set_log_sink(previous_sink);
    skl::abix::util::set_log_level(previous_level);
    log_info("after sink/level restore, Diagnostic state is back to the test default");
}

TEST_CASE("20.logging_truncation", "[log][prompt20]") {
    log_info("Test 20: log buffer truncation — legacy and length-aware callbacks");

    const auto previous_sink = skl::abix::util::get_log_sink();
    const auto previous_record_sink = skl::abix::util::get_log_record_sink();
    const auto previous_level = skl::abix::util::get_log_level();
    skl::abix::util::set_log_level(skl::abix::util::LogLevel::Debug);

    static uint32_t last_len = 0;
    static bool last_truncated = false;
    static char last_record_msg[1'100] = {};

    auto record_sink = [](const skl::abix::util::LogRecord *record) {
        last_len = record->length;
        last_truncated = record->truncated;
        snprintf(last_record_msg, sizeof(last_record_msg), "%s", record->message);
    };

    skl::abix::util::set_log_record_sink(record_sink);

    std::string long_msg(1'500, 'X');
    ABIX_LOG_INFO("%s", long_msg.c_str());
    REQUIRE(last_truncated);
    REQUIRE(last_len == skl::abix::util::log_buffer_size - 1);
    REQUIRE(strlen(last_record_msg) == last_len);

    ABIX_LOG_INFO("short");
    REQUIRE_FALSE(last_truncated);
    REQUIRE(last_len == 5);
    REQUIRE(strcmp(last_record_msg, "short") == 0);

    // The legacy callback still works, but deliberately has no truncation flag.
    static int legacy_len = 0;
    auto legacy_sink = [](skl::abix::util::LogLevel, const char *msg) { legacy_len = static_cast<int>(strlen(msg)); };
    skl::abix::util::set_log_sink(legacy_sink);
    ABIX_LOG_INFO("%s", long_msg.c_str());
    REQUIRE(legacy_len == static_cast<int>(skl::abix::util::log_buffer_size - 1));

    if (previous_record_sink != nullptr)
        skl::abix::util::set_log_record_sink(previous_record_sink);
    else
        skl::abix::util::set_log_sink(previous_sink);
    skl::abix::util::set_log_level(previous_level);
}

TEST_CASE("21.logging_explicit_state", "[log][state]") {
    log_info("Test 21: explicit LogState ownership");

    skl::abix::util::LogState state;
    REQUIRE(skl::abix::util::get_log_level(state) == skl::abix::util::LogLevel::Warning);

    static int count = 0;
    auto sink = [](skl::abix::util::LogLevel, const char *) { ++count; };

    // Default Warning: Info is filtered out.
    skl::abix::util::set_log_sink(state, sink);
    skl::abix::util::log(state, skl::abix::util::LogLevel::Info, "filtered");
    REQUIRE(count == 0);

    skl::abix::util::set_log_level(state, skl::abix::util::LogLevel::Debug);
    skl::abix::util::log(state, skl::abix::util::LogLevel::Info, "explicit %d", 1);
    REQUIRE(count == 1);

    ABIX_LOG_INFO_TO(state, "explicit macro %d", 2);
    REQUIRE(count == 2);

    // Extended callback and legacy callback are mutually exclusive.
    skl::abix::util::set_log_record_sink(state, [](const skl::abix::util::LogRecord *) { ++count; });
    REQUIRE(skl::abix::util::get_log_sink(state) == nullptr);
    ABIX_LOG_WARNING_TO(state, "record macro");
    REQUIRE(count == 3);

    skl::abix::util::set_log_level(state, skl::abix::util::LogLevel::Off);
    ABIX_LOG_ERROR_TO(state, "off");
    REQUIRE(count == 3);
}

TEST_CASE("22.logging_concurrent_state", "[log][thread]") {
    log_info("Test 22: concurrent set_log_sink/log calls are race-free at the API boundary");

    skl::abix::util::LogState state;
    skl::abix::util::set_log_level(state, skl::abix::util::LogLevel::Debug);

    static std::atomic<int> delivered{0};
    auto sink = [](skl::abix::util::LogLevel, const char *) { delivered.fetch_add(1, std::memory_order_relaxed); };

    skl::abix::util::set_log_sink(state, sink);
    delivered.store(0);

    std::thread setter([&state, sink]() {
        for (int i = 0; i < 1'000; ++i) {
            skl::abix::util::set_log_sink(state, nullptr);
            skl::abix::util::set_log_sink(state, sink);
        }
    });

    std::vector<std::thread> loggers;
    for (int i = 0; i < 4; ++i) {
        loggers.emplace_back([&state]() {
            for (int j = 0; j < 1'000; ++j)
                skl::abix::util::log(state, skl::abix::util::LogLevel::Info, "concurrent %d", j);
        });
    }

    setter.join();
    for (auto &logger : loggers)
        logger.join();

    skl::abix::util::set_log_sink(state, sink);
    const int before = delivered.load(std::memory_order_relaxed);
    skl::abix::util::log(state, skl::abix::util::LogLevel::Info, "after concurrent writers");
    REQUIRE(delivered.load(std::memory_order_relaxed) > before);
}

TEST_CASE("23.logging_reentrant_callback", "[log][reentrancy]") {
    log_info("Test 23: a callback can update logging state without an ABIX lock");

    static skl::abix::util::LogState state;
    skl::abix::util::set_log_level(state, skl::abix::util::LogLevel::Debug);

    static int calls = 0;
    skl::abix::util::set_log_sink(state, [](skl::abix::util::LogLevel, const char *) {
        ++calls;
        skl::abix::util::set_log_level(state, skl::abix::util::LogLevel::Warning);
    });

    skl::abix::util::log(state, skl::abix::util::LogLevel::Error, "one");
    REQUIRE(calls == 1);
    REQUIRE(skl::abix::util::get_log_level(state) == skl::abix::util::LogLevel::Warning);
    skl::abix::util::log(state, skl::abix::util::LogLevel::Info, "filtered after callback");
    REQUIRE(calls == 1);
}
