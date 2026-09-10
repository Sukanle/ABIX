#include <catch2/catch_test_macros.hpp>

#include "abix/abi_model.h"
#include "abix/bootstrap.h"

namespace {
struct Sink {
    const skl::abix::bootstrap::BootstrapRecord *records = nullptr;
    uint32_t count = 0;
};

void consume(const skl::abix::bootstrap::BootstrapRecord *records,
             uint32_t count, void *context) {
    auto *sink = static_cast<Sink *>(context);
    sink->records = records;
    sink->count = count;
}
} // namespace

TEST_CASE("ABI model keeps fixed-width metadata layout") {
    STATIC_REQUIRE(sizeof(skl::abix::model::Hash128) == 16);
    STATIC_REQUIRE(sizeof(skl::abix::model::TypeDesc) == 32);
    STATIC_REQUIRE(sizeof(skl::abix::model::TypeLayout) == 32);
    STATIC_REQUIRE(sizeof(skl::abix::model::Field) == 40);
}

TEST_CASE("bootstrap validates and forwards immutable records") {
    const int metadata = 42;
    const skl::abix::bootstrap::BootstrapRecord records[] = {
        {0x1234, sizeof(metadata), alignof(decltype(metadata)), &metadata},
    };
    Sink sink;
    const skl::abix::bootstrap::BootstrapImage image{
        records, 1, skl::abix::bootstrap::BOOTSTRAP_FORMAT_VERSION,
        skl::abix::bootstrap::BOOTSTRAP_LITTLE_ENDIAN, consume, &sink};

    REQUIRE(skl::abix::bootstrap::abix_bootstrap(image) ==
            skl::abix::bootstrap::Status::ok);
    REQUIRE(sink.records == records);
    REQUIRE(sink.count == 1);
}

TEST_CASE("bootstrap rejects malformed records") {
    Sink sink;
    const skl::abix::bootstrap::BootstrapRecord invalid[] = {
        {0, 1, 1, &sink},
    };
    const skl::abix::bootstrap::BootstrapImage image{
        invalid, 1, skl::abix::bootstrap::BOOTSTRAP_FORMAT_VERSION,
        skl::abix::bootstrap::BOOTSTRAP_LITTLE_ENDIAN, consume, &sink};

    REQUIRE(skl::abix::bootstrap::abix_bootstrap(image) ==
            skl::abix::bootstrap::Status::invalid_image);
    REQUIRE(sink.records == nullptr);
}
