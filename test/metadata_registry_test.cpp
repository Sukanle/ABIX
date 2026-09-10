#include <catch2/catch_test_macros.hpp>

#include "abix/metadata_registry.h"
#include "abix/runtime_registry.h"

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

TEST_CASE("runtime registry bridges generated descriptors atomically") {
    using namespace skl::abix;
    constexpr model::TypeId int_id{0x11, 0x22};
    constexpr model::TypeId foo_id{0x33, 0x44};
    static const runtime::FieldDescriptor fields[] = {
        {"value", int_id, 0, 0},
    };
    static const runtime::TypeDescriptor types[] = {
        {"int", int_id, {0x51, 0x61}, 0, 4, 4, nullptr, 0},
        {"Foo", foo_id, {0x71, 0x81}, 0, 4, 4, fields, 1},
    };
    static const runtime::ModuleDescriptor module{
        "bridge", "1", types, 2, nullptr, 0, nullptr, 0};

    runtime::RuntimeRegistry<4> registry;
    REQUIRE(registry.register_module(module) == runtime::RuntimeRegisterStatus::ok);
    REQUIRE(registry.size() == 2);
    REQUIRE(registry.find_by_id(foo_id) != nullptr);
    REQUIRE(registry.find_by_name("Foo") != nullptr);
    REQUIRE(registry.find_by_id(foo_id)->layout->layout_hash == model::Hash128{0x71, 0x81});
    REQUIRE(registry.canonical().find_by_id(foo_id) != nullptr);

    REQUIRE(registry.register_module(module) == runtime::RuntimeRegisterStatus::duplicate_type);
    REQUIRE(registry.size() == 2);
}

TEST_CASE("runtime registry rejects unknown references without partial registration") {
    using namespace skl::abix;
    constexpr model::TypeId foo_id{0x33, 0x44};
    constexpr model::TypeId missing_id{0x55, 0x66};
    static const runtime::FieldDescriptor fields[] = {
        {"missing", missing_id, 0, 0},
    };
    static const runtime::TypeDescriptor type{
        "Foo", foo_id, {0x71, 0x81}, 0, 4, 4, fields, 1};
    static const runtime::ModuleDescriptor module{
        "invalid", "1", &type, 1, nullptr, 0, nullptr, 0};

    runtime::RuntimeRegistry<4> registry;
    REQUIRE(registry.register_module(module) == runtime::RuntimeRegisterStatus::unknown_type_reference);
    REQUIRE(registry.size() == 0);
}
