#include <catch2/catch_test_macros.hpp>

#include "abix/metadata_registry.h"

TEST_CASE("bootstrap records are promoted into the single-thread registry") {
    using namespace skl::abix;
    const model::TypeDesc desc{{0x11, 0x22}, 0, 0, 0};
    const model::TypeLayout layout{sizeof(uint64_t), alignof(uint64_t), 0, 0,
                                   {0x33, 0x44}};
    const runtime::BootstrapMetadata metadata{&desc, &layout};
    const bootstrap::BootstrapRecord record{0x1234, sizeof(desc), alignof(model::TypeDesc),
                                            &metadata};
    runtime::MetadataRegistry<2> registry;
    const bootstrap::BootstrapImage image{
        &record, 1, bootstrap::BOOTSTRAP_FORMAT_VERSION,
        bootstrap::BOOTSTRAP_LITTLE_ENDIAN,
        runtime::MetadataRegistry<2>::consume_bootstrap, &registry};

    REQUIRE(bootstrap::abix_bootstrap(image) == bootstrap::Status::ok);
    REQUIRE(registry.size() == 1);
    const auto *entry = registry.find_by_id(desc.id);
    REQUIRE(entry != nullptr);
    REQUIRE(entry->layout == &layout);
    REQUIRE(entry->bootstrap_hash == 0x1234);
}

TEST_CASE("metadata registry validates descriptors and rejects duplicates") {
    using namespace skl::abix;
    const model::TypeDesc desc{{1, 0}, 0, 0, 0};
    const model::TypeLayout layout{4, 4, 0, 0, {1, 0}};
    runtime::MetadataRegistry<1> registry;

    REQUIRE(registry.register_type(&desc, &layout) ==
            runtime::RegisterStatus::ok);
    REQUIRE(registry.register_type(&desc, &layout) ==
            runtime::RegisterStatus::duplicate_type);
    REQUIRE(registry.register_type(nullptr, &layout) ==
            runtime::RegisterStatus::invalid_descriptor);
}

TEST_CASE("registry bootstraps metadata describing its own entry format") {
    skl::abix::runtime::MetadataRegistry<4> registry;

    REQUIRE(registry.bootstrap_self() == skl::abix::bootstrap::Status::ok);
    REQUIRE(registry.size() == 2);
    REQUIRE(registry.at(0) != nullptr);
    REQUIRE(registry.at(1) != nullptr);
    REQUIRE(registry.at(0)->layout->size == sizeof(skl::abix::runtime::RegistryEntry));
}
