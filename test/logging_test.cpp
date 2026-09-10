#include "test_common.hpp"
#include <cstring>

TEST_CASE("19.logging_basic", "[log][prompt19]") {
    log_info("Test 19: basic logging — verify log levels, set_log_sink, and ABIX_LOG_* macros");

    static int log_count = 0;
    static skl::abix::LogLevel last_level = skl::abix::LogLevel::Debug;
    static char last_msg[256] = {};

    auto sink = [](skl::abix::LogLevel level, const char *msg) {
        ++log_count;
        last_level = level;
        std::snprintf(last_msg, sizeof(last_msg), "%s", msg);
    };

    skl::abix::set_log_sink(sink);
    log_count = 0;

    ABIX_LOG_DEBUG("debug message %d", 1);
    REQUIRE(log_count == 1);
    REQUIRE(last_level == skl::abix::LogLevel::Debug);
    REQUIRE(std::strstr(last_msg, "debug message 1") != nullptr);
    log_info("log sink received DEBUG: [%s]", last_msg);

    ABIX_LOG_INFO("info message %d", 2);
    REQUIRE(log_count == 2);
    REQUIRE(last_level == skl::abix::LogLevel::Info);
    REQUIRE(std::strstr(last_msg, "info message 2") != nullptr);
    log_info("log sink received INFO: [%s]", last_msg);

    ABIX_LOG_WARNING("warning message %d", 3);
    REQUIRE(log_count == 3);
    REQUIRE(last_level == skl::abix::LogLevel::Warning);
    REQUIRE(std::strstr(last_msg, "warning message 3") != nullptr);
    log_info("log sink received WARNING: [%s]", last_msg);

    ABIX_LOG_ERROR("error message %d", 4);
    REQUIRE(log_count == 4);
    REQUIRE(last_level == skl::abix::LogLevel::Error);
    REQUIRE(std::strstr(last_msg, "error message 4") != nullptr);
    log_info("log sink received ERROR: [%s]", last_msg);

    skl::abix::set_log_sink(nullptr);
    log_count = 0;
    ABIX_LOG_INFO("should not appear");
    REQUIRE(log_count == 0);
    log_info("after set_log_sink(nullptr), no log is emitted");
}

TEST_CASE("20.logging_truncation", "[log][prompt20]") {
    log_info("Test 20: log buffer truncation — messages exceeding 1024 bytes are truncated safely");

    static bool truncated = false;
    static int last_len = 0;

    auto sink = [](skl::abix::LogLevel level, const char *msg) {
        (void)level;
        last_len = (int)std::strlen(msg);
        truncated = (last_len < 2'000);
    };

    skl::abix::set_log_sink(sink);
    truncated = false;

    std::string long_msg(1'500, 'X');
    ABIX_LOG_INFO("%s", long_msg.c_str());

    REQUIRE(truncated);
    REQUIRE(last_len < 1'500);
    log_info("long message of %zu chars truncated to %d chars (buffer=1024)", long_msg.size(), last_len);

    skl::abix::set_log_sink(nullptr);
}