#include "SKL/ABIX/Util/Log.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/Logger.h"
#include "quill/sinks/FileSink.h"

namespace {
quill::Logger *g_quill_logger = nullptr;

void quill_sink(skl::abix::util::LogLevel level, const char *message) {
    if (g_quill_logger == nullptr) return;

    switch (level) {
        case skl::abix::util::LogLevel::Debug:   LOG_DEBUG(g_quill_logger, "[ABIX] {}", message); break;
        case skl::abix::util::LogLevel::Info:    LOG_INFO(g_quill_logger, "[ABIX] {}", message); break;
        case skl::abix::util::LogLevel::Warning: LOG_WARNING(g_quill_logger, "[ABIX] {}", message); break;
        case skl::abix::util::LogLevel::Error:   LOG_ERROR(g_quill_logger, "[ABIX] {}", message); break;
        case skl::abix::util::LogLevel::Off:     break;
    }
}
}   // namespace

TEST_CASE("25.logging_quill_sink", "[log][quill][integration]") {
    constexpr const char *log_path = "abix_quill_test.log";

    quill::Backend::start();

    quill::FileSinkConfig config;
    config.set_open_mode('w');
    config.set_filename_append_option(quill::FilenameAppendOption::None);

    auto file_sink = quill::Frontend::create_or_get_sink<quill::FileSink>(log_path, config);
    g_quill_logger = quill::Frontend::create_or_get_logger("abix_quill_test", std::move(file_sink));
    g_quill_logger->set_log_level(quill::LogLevel::TraceL3);

    skl::abix::util::set_log_level(skl::abix::util::LogLevel::Debug);
    skl::abix::util::set_log_sink(&quill_sink);

    ABIX_LOG_INFO("quill integration %d", 42);
    ABIX_LOG_ERROR("quill error path");

    // Quill owns backend/queue concurrency; ABIX only owns atomic sink/config
    // reads on this path.
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([]() {
            for (int i = 0; i < 10; ++i)
                ABIX_LOG_INFO("quill concurrent %d", i);
        });
    }
    for (auto &worker : workers)
        worker.join();

    skl::abix::util::set_log_sink(nullptr);
    g_quill_logger->flush_log();
    quill::Backend::stop();
    g_quill_logger = nullptr;

    std::ifstream input(log_path);
    REQUIRE(input.good());
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    REQUIRE(contents.find("quill integration 42") != std::string::npos);
    REQUIRE(contents.find("quill error path") != std::string::npos);
    REQUIRE(contents.find("quill concurrent 0") != std::string::npos);

    input.close();
    std::remove(log_path);
}
