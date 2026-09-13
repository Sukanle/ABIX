#include <catch2/catch_test_macros.hpp>

#include "abix/metadata_registry.h"
#include "abix/runtime_registry.h"

TEST_CASE("bootstrap records are promoted into the single-thread registry") {
    using namespace skl::abix;
    const model::TypeDesc desc{
        {0x11, 0x22},
        0, 0, 0
    };
    const model::TypeLayout layout{
        sizeof(uint64_t), alignof(uint64_t), 0, 0, {0x33, 0x44}
    };
    const runtime::BootstrapMetadata metadata{&desc, &layout};
    const bootstrap::BootstrapRecord record{0x1234, sizeof(desc), alignof(model::TypeDesc), &metadata};
    runtime::MetadataRegistry<2> registry;
    const bootstrap::BootstrapImage image{&record, 1, bootstrap::BOOTSTRAP_FORMAT_VERSION,
        bootstrap::BOOTSTRAP_LITTLE_ENDIAN, runtime::MetadataRegistry<2>::consume_bootstrap, &registry};

    REQUIRE(bootstrap::abix_bootstrap(image) == bootstrap::Status::ok);
    REQUIRE(registry.size() == 1);
    const auto *entry = registry.find_by_id(desc.id);
    REQUIRE(entry != nullptr);
    REQUIRE(entry->layout == &layout);
    REQUIRE(entry->bootstrap_hash == 0x1234);
}

TEST_CASE("metadata registry validates descriptors and classifies shared types") {
    using namespace skl::abix;
    const model::TypeDesc desc{
        {1, 0},
        0, 0, 0
    };
    const model::TypeLayout layout{
        4, 4, 0, 0, {1, 0}
    };
    const model::TypeLayout conflicting{
        8, 8, 0, 0, {2, 0}
    };
    runtime::MetadataRegistry<2> registry;

    REQUIRE(registry.register_type(&desc, &layout) == runtime::RegisterStatus::ok);
    // Same TypeID + same LayoutHash: a compatible shared type, deduplicated.
    REQUIRE(registry.register_type(&desc, &layout) == runtime::RegisterStatus::already_registered);
    // Same TypeID + different LayoutHash: Boundary #1 ABI conflict.
    REQUIRE(registry.register_type(&desc, &conflicting) == runtime::RegisterStatus::layout_conflict);
    REQUIRE(registry.register_type(nullptr, &layout) == runtime::RegisterStatus::invalid_descriptor);
    REQUIRE(registry.size() == 1);
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
        {int_id, 0, 0},
    };
    static const runtime::TypeDescriptor types[] = {
        {nullptr, int_id, {0x51, 0x61}, 0, 4, 4, 0},
        { fields, foo_id, {0x71, 0x81}, 0, 4, 4, 1},
    };
    // Canonical TypeDesc/TypeLayout — the one ABI truth.
    // Must be consistent with the runtime projection above.
    static const model::TypeDesc canonical_types[] = {
        {int_id, 0, 0, 0},
        {foo_id, 0, 0, 1},
    };
    static const model::TypeLayout canonical_layouts[] = {
        {4, 4, 0, 0, {0x51, 0x61}},
        {4, 4, 0, 1, {0x71, 0x81}},
    };
    static const runtime::ModuleDescriptor module{
        "bridge", "1", types, canonical_types, canonical_layouts, 2, 0, nullptr, nullptr, 0};

    runtime::RuntimeRegistry<4> registry;
    REQUIRE(registry.register_module(module) == runtime::RuntimeRegisterStatus::ok);
    REQUIRE(registry.size() == 2);
    REQUIRE(registry.find_by_id(foo_id) != nullptr);
    // Verify the canonical TypeLayout is used directly (not reconstructed).
    REQUIRE(registry.find_by_id(foo_id)->layout == &canonical_layouts[1]);
    REQUIRE(registry.find_by_id(foo_id)->layout->layout_hash == model::Hash128{0x71, 0x81});
    REQUIRE(registry.canonical().find_by_id(foo_id) != nullptr);
    // Verify the canonical TypeDesc is imported directly.
    REQUIRE(registry.find_by_id(foo_id)->canonical == &canonical_types[1]);

    // Re-registering a module whose type ids and layouts match is a no-op:
    // the shared types are deduplicated rather than rejected.
    REQUIRE(registry.register_module(module) == runtime::RuntimeRegisterStatus::ok);
    REQUIRE(registry.size() == 2);
    REQUIRE(registry.validate() == runtime::RuntimeRegisterStatus::ok);
}

TEST_CASE("runtime registry validates shared type ids across modules") {
    using namespace skl::abix;
    constexpr model::TypeId shared_id{0x10, 0x20};

    static const runtime::TypeDescriptor a_types[] = {
        {nullptr, shared_id, {0xA1, 0xA2}, 0, 4, 4, 0},
    };
    static const model::TypeDesc a_canonical[] = {
        {shared_id, 0, 0, 0}
    };
    static const model::TypeLayout a_layouts[] = {
        {4, 4, 0, 0, {0xA1, 0xA2}}
    };
    static const runtime::ModuleDescriptor module_a{
        "a", "1", a_types, a_canonical, a_layouts, 1, 0, nullptr, nullptr, 0};

    // Module B shares the exact same type identity and layout.
    static const runtime::TypeDescriptor b_types[] = {
        {nullptr, shared_id, {0xA1, 0xA2}, 0, 4, 4, 0},
    };
    static const model::TypeDesc b_canonical[] = {
        {shared_id, 0, 0, 0}
    };
    static const model::TypeLayout b_layouts[] = {
        {4, 4, 0, 0, {0xA1, 0xA2}}
    };
    static const runtime::ModuleDescriptor module_b{
        "b", "1", b_types, b_canonical, b_layouts, 1, 0, nullptr, nullptr, 0};

    // Module C claims the same TypeID with a different LayoutHash.
    static const runtime::TypeDescriptor c_types[] = {
        {nullptr, shared_id, {0xB1, 0xB2}, 0, 8, 8, 0},
    };
    static const model::TypeDesc c_canonical[] = {
        {shared_id, 0, 0, 0}
    };
    static const model::TypeLayout c_layouts[] = {
        {8, 8, 0, 0, {0xB1, 0xB2}}
    };
    static const runtime::ModuleDescriptor module_c{
        "c", "1", c_types, c_canonical, c_layouts, 1, 0, nullptr, nullptr, 0};

    runtime::RuntimeRegistry<4> registry;
    REQUIRE(registry.register_module(module_a) == runtime::RuntimeRegisterStatus::ok);
    REQUIRE(registry.size() == 1);

    REQUIRE(registry.check_module(module_b) == runtime::RuntimeRegisterStatus::ok);
    REQUIRE(registry.register_module(module_b) == runtime::RuntimeRegisterStatus::ok);
    REQUIRE(registry.size() == 1);   // shared type, not a second entry
    REQUIRE(registry.find_by_id(shared_id)->layout == &a_layouts[0]);

    REQUIRE(registry.check_module(module_c) == runtime::RuntimeRegisterStatus::layout_conflict);
    REQUIRE(registry.register_module(module_c) == runtime::RuntimeRegisterStatus::layout_conflict);
    REQUIRE(registry.size() == 1);
    REQUIRE(registry.find_by_id(shared_id)->layout == &a_layouts[0]);
    REQUIRE(registry.validate() == runtime::RuntimeRegisterStatus::ok);
}

TEST_CASE("runtime registry rejects unknown references without partial registration") {
    using namespace skl::abix;
    constexpr model::TypeId foo_id{0x33, 0x44};
    constexpr model::TypeId missing_id{0x55, 0x66};
    static const runtime::FieldDescriptor fields[] = {
        {missing_id, 0, 0},
    };
    static const runtime::TypeDescriptor type{
        fields, foo_id, {0x71, 0x81},
          0, 4, 4, 1
    };
    static const model::TypeDesc canonical_types[] = {
        {foo_id, 0, 0, 0},
    };
    static const model::TypeLayout canonical_layouts[] = {
        {4, 4, 0, 1, {0x71, 0x81}},
    };
    static const runtime::ModuleDescriptor module{
        "invalid", "1", &type, canonical_types, canonical_layouts, 1, 0, nullptr, nullptr, 0};

    runtime::RuntimeRegistry<4> registry;
    REQUIRE(registry.register_module(module) == runtime::RuntimeRegisterStatus::unknown_type_reference);
    REQUIRE(registry.size() == 0);
}