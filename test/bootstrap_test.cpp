#include <catch2/catch_test_macros.hpp>

#include "ABIX/Model/ABI.h"
#include "ABIX/Model/Bootstrap.h"

namespace {
struct Sink {
    const skl::abix::model::BootRecord *records = nullptr;
    uint32_t count = 0;
};

void consume(const skl::abix::model::BootRecord *records, uint32_t count, void *context) {
    auto *sink = static_cast<Sink *>(context);
    sink->records = records;
    sink->count = count;
}
}   // namespace

TEST_CASE("ABI model keeps fixed-width metadata layout") {
    STATIC_REQUIRE(sizeof(skl::abix::Hash128) == 16);
    STATIC_REQUIRE(sizeof(skl::abix::model::TypeDesc) == 32);
    STATIC_REQUIRE(sizeof(skl::abix::model::TypeLayout) == 32);
    STATIC_REQUIRE(sizeof(skl::abix::model::Field) == 40);
}

TEST_CASE("bootstrap validates and forwards immutable records") {
    const int metadata = 42;
    const skl::abix::model::BootRecord records[] = {
        {0X1234, sizeof(metadata), alignof(decltype(metadata)), &metadata},
    };
    Sink sink;
    const skl::abix::model::BootImage image{
        records, 1, SKL_ABIX_BOOTSTRAP_FORMAT_VERSION, SKL_ABIX_BOOTSTRAP_LITTLE_ENDIAN, consume, &sink};

    REQUIRE(skl::abix::model::abix_bootstrap(image) == skl::abix::model::BootStatus::ok);
    REQUIRE(sink.records == records);
    REQUIRE(sink.count == 1);
}

TEST_CASE("bootstrap rejects malformed records") {
    Sink sink;
    const skl::abix::model::BootRecord invalid[] = {
        {0, 1, 1, &sink},
    };
    const skl::abix::model::BootImage image{
        invalid, 1, SKL_ABIX_BOOTSTRAP_FORMAT_VERSION, SKL_ABIX_BOOTSTRAP_FORMAT_VERSION, consume, &sink};

    REQUIRE(skl::abix::model::abix_bootstrap(image) == skl::abix::model::BootStatus::invalid_image);
    REQUIRE(sink.records == nullptr);
}
