#include <catch2/catch_test_macros.hpp>

#include "ABIX/Metadata/Registry.h"
#include "ABIX/Metadata/Descriptor.h"
#include "ABIX/Runtime/Registry.h"

TEST_CASE("bootstrap records are promoted into the single-thread registry") {
    using namespace skl::abix;
    const model::TypeDesc desc{
        {0x11, 0x22},
        0, 0, 0
    };
    const model::TypeLayout layout{
        sizeof(uint64_t), alignof(uint64_t), 0, 0, {0x33, 0x44}
    };
    const metadata::Bootstrap metadata{&desc, &layout};
    const model::BootRecord record{0x1234, sizeof(desc), alignof(model::TypeDesc), &metadata};
    metadata::Registry<2> registry;
    const model::BootImage image{&record, 1, SKL_ABIX_BOOTSTRAP_FORMAT_VERSION, SKL_ABIX_BOOTSTRAP_LITTLE_ENDIAN,
        metadata::Registry<2>::consume_bootstrap, &registry};

    REQUIRE(model::abix_bootstrap(image) == model::BootStatus::ok);
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
    metadata::Registry<2> registry;

    REQUIRE(registry.register_type(&desc, &layout) == metadata::RegisterStatus::ok);
    // Same TypeID + same LayoutHash: a compatible shared type, deduplicated.
    REQUIRE(registry.register_type(&desc, &layout) == metadata::RegisterStatus::already_registered);
    // Same TypeID + different LayoutHash: Boundary #1 ABI conflict.
    REQUIRE(registry.register_type(&desc, &conflicting) == metadata::RegisterStatus::layout_conflict);
    REQUIRE(registry.register_type(nullptr, &layout) == metadata::RegisterStatus::invalid_descriptor);
    REQUIRE(registry.size() == 1);
}

TEST_CASE("registry bootstraps metadata describing its own entry format") {
    skl::abix::metadata::Registry<4> registry;

    REQUIRE(registry.bootstrap_self() == skl::abix::model::BootStatus::ok);
    REQUIRE(registry.size() == 2);
    REQUIRE(registry.at(0) != nullptr);
    REQUIRE(registry.at(1) != nullptr);
    REQUIRE(registry.at(0)->layout->size == sizeof(skl::abix::metadata::RegistryEntry));
}

TEST_CASE("runtime registry bridges generated descriptors atomically") {
    using namespace skl::abix;
    constexpr model::TypeId int_id{0x11, 0x22};
    constexpr model::TypeId foo_id{0x33, 0x44};
    static const metadata::FieldDescriptor fields[] = {
        {int_id, 0, 0},
    };
    static const metadata::TypeDescriptor types[] = {
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
    static const metadata::ModuleDescriptor module{
        "bridge", "1", types, canonical_types, canonical_layouts, 2, 0, nullptr, nullptr, 0};

    runtime::Registry<4> registry;
    REQUIRE(registry.register_module(module) == runtime::RegisterStatus::ok);
    REQUIRE(registry.size() == 2);
    REQUIRE(registry.find_by_id(foo_id) != nullptr);
    // Verify the canonical TypeLayout is used directly (not reconstructed).
    REQUIRE(registry.find_by_id(foo_id)->layout == &canonical_layouts[1]);
    REQUIRE(registry.find_by_id(foo_id)->layout->layout_hash == Hash128{0x71, 0x81});
    REQUIRE(registry.canonical().find_by_id(foo_id) != nullptr);
    // Verify the canonical TypeDesc is imported directly.
    REQUIRE(registry.find_by_id(foo_id)->canonical == &canonical_types[1]);

    // Re-registering a module whose type ids and layouts match is a no-op:
    // the shared types are deduplicated rather than rejected.
    REQUIRE(registry.register_module(module) == runtime::RegisterStatus::ok);
    REQUIRE(registry.size() == 2);
    REQUIRE(registry.validate() == runtime::RegisterStatus::ok);
}

TEST_CASE("runtime registry validates shared type ids across modules") {
    using namespace skl::abix;
    constexpr model::TypeId shared_id{0x10, 0x20};

    static const metadata::TypeDescriptor a_types[] = {
        {nullptr, shared_id, {0xA1, 0xA2}, 0, 4, 4, 0},
    };
    static const model::TypeDesc a_canonical[] = {
        {shared_id, 0, 0, 0}
    };
    static const model::TypeLayout a_layouts[] = {
        {4, 4, 0, 0, {0xA1, 0xA2}}
    };
    static const metadata::ModuleDescriptor module_a{
        "a", "1", a_types, a_canonical, a_layouts, 1, 0, nullptr, nullptr, 0};

    // Module B shares the exact same type identity and layout.
    static const metadata::TypeDescriptor b_types[] = {
        {nullptr, shared_id, {0xA1, 0xA2}, 0, 4, 4, 0},
    };
    static const model::TypeDesc b_canonical[] = {
        {shared_id, 0, 0, 0}
    };
    static const model::TypeLayout b_layouts[] = {
        {4, 4, 0, 0, {0xA1, 0xA2}}
    };
    static const metadata::ModuleDescriptor module_b{
        "b", "1", b_types, b_canonical, b_layouts, 1, 0, nullptr, nullptr, 0};

    // Module C claims the same TypeID with a different LayoutHash.
    static const metadata::TypeDescriptor c_types[] = {
        {nullptr, shared_id, {0xB1, 0xB2}, 0, 8, 8, 0},
    };
    static const model::TypeDesc c_canonical[] = {
        {shared_id, 0, 0, 0}
    };
    static const model::TypeLayout c_layouts[] = {
        {8, 8, 0, 0, {0xB1, 0xB2}}
    };
    static const metadata::ModuleDescriptor module_c{
        "c", "1", c_types, c_canonical, c_layouts, 1, 0, nullptr, nullptr, 0};

    runtime::Registry<4> registry;
    REQUIRE(registry.register_module(module_a) == runtime::RegisterStatus::ok);
    REQUIRE(registry.size() == 1);

    REQUIRE(registry.check_module(module_b) == runtime::RegisterStatus::ok);
    REQUIRE(registry.register_module(module_b) == runtime::RegisterStatus::ok);
    REQUIRE(registry.size() == 1);   // shared type, not a second entry
    REQUIRE(registry.find_by_id(shared_id)->layout == &a_layouts[0]);

    REQUIRE(registry.check_module(module_c) == runtime::RegisterStatus::layout_conflict);
    REQUIRE(registry.register_module(module_c) == runtime::RegisterStatus::layout_conflict);
    REQUIRE(registry.size() == 1);
    REQUIRE(registry.find_by_id(shared_id)->layout == &a_layouts[0]);
    REQUIRE(registry.validate() == runtime::RegisterStatus::ok);
}

TEST_CASE("runtime registry dispatches a TypeID across module versions") {
    using namespace skl::abix;
    constexpr model::TypeId shared_id{0x0A, 0x0B};

    // Version 1: a 4-byte type. Version 2: the same TypeID grew to 8 bytes.
    static const metadata::TypeDescriptor v1_types[] = {
        {nullptr, shared_id, {0xC1, 0xC2}, 0, 4, 4, 0},
    };
    static const model::TypeDesc v1_canonical[] = {
        {shared_id, 0, 0, 0}
    };
    static const model::TypeLayout v1_layouts[] = {
        {4, 4, 0, 0, {0xC1, 0xC2}}
    };
    static const metadata::ModuleDescriptor module_v1{
        "pkg", "1", v1_types, v1_canonical, v1_layouts, 1, 0, nullptr, nullptr, 0};

    static const metadata::TypeDescriptor v2_types[] = {
        {nullptr, shared_id, {0xD1, 0xD2}, 0, 8, 8, 0},
    };
    static const model::TypeDesc v2_canonical[] = {
        {shared_id, 0, 0, 0}
    };
    static const model::TypeLayout v2_layouts[] = {
        {8, 8, 0, 0, {0xD1, 0xD2}}
    };
    static const metadata::ModuleDescriptor module_v2{
        "pkg", "2", v2_types, v2_canonical, v2_layouts, 1, 0, nullptr, nullptr, 0};

    // A second claim on version 2 with a different layout stays a conflict.
    static const metadata::TypeDescriptor v2b_types[] = {
        {nullptr, shared_id, {0xE1, 0xE2}, 0, 16, 16, 0},
    };
    static const model::TypeDesc v2b_canonical[] = {
        {shared_id, 0, 0, 0}
    };
    static const model::TypeLayout v2b_layouts[] = {
        {16, 16, 0, 0, {0xE1, 0xE2}}
    };
    static const metadata::ModuleDescriptor module_v2b{
        "pkg", "2", v2b_types, v2b_canonical, v2b_layouts, 1, 0, nullptr, nullptr, 0};

    REQUIRE(runtime::Registry<4>::parse_version("1.0") == 1);
    REQUIRE(runtime::Registry<4>::module_version(module_v2) == 2);

    runtime::Registry<4> registry;
    REQUIRE(registry.register_module(module_v1) == runtime::RegisterStatus::ok);
    REQUIRE(registry.size() == 1);
    REQUIRE(registry.find_type(shared_id, 1)->layout == &v1_layouts[0]);
    REQUIRE(registry.find_type(shared_id, 2) == nullptr);

    // Same TypeID under a newer version coexists instead of conflicting.
    REQUIRE(registry.check_module(module_v2) == runtime::RegisterStatus::ok);
    REQUIRE(registry.register_module(module_v2) == runtime::RegisterStatus::ok);
    REQUIRE(registry.size() == 2);
    REQUIRE(registry.find_type(shared_id, 1)->layout == &v1_layouts[0]);
    REQUIRE(registry.find_type(shared_id, 2)->layout == &v2_layouts[0]);
    // Unversioned lookup resolves to the newest version.
    REQUIRE(registry.find_by_id(shared_id) == registry.find_type(shared_id, 2));

    // Re-registering version 1 is a no-op; version 2 stays intact.
    REQUIRE(registry.register_module(module_v1) == runtime::RegisterStatus::ok);
    REQUIRE(registry.size() == 2);
    REQUIRE(registry.find_by_id(shared_id)->layout == &v2_layouts[0]);

    REQUIRE(registry.register_module(module_v2b) == runtime::RegisterStatus::layout_conflict);
    REQUIRE(registry.size() == 2);
    REQUIRE(registry.validate() == runtime::RegisterStatus::ok);
}

TEST_CASE("runtime registry rejects unknown references without partial registration") {
    using namespace skl::abix;
    constexpr model::TypeId foo_id{0x33, 0x44};
    constexpr model::TypeId missing_id{0x55, 0x66};
    static const metadata::FieldDescriptor fields[] = {
        {missing_id, 0, 0},
    };
    static const metadata::TypeDescriptor type{
        fields, foo_id, {0x71, 0x81},
          0, 4, 4, 1
    };
    static const model::TypeDesc canonical_types[] = {
        {foo_id, 0, 0, 0},
    };
    static const model::TypeLayout canonical_layouts[] = {
        {4, 4, 0, 1, {0x71, 0x81}},
    };
    static const metadata::ModuleDescriptor module{
        "invalid", "1", &type, canonical_types, canonical_layouts, 1, 0, nullptr, nullptr, 0};

    runtime::Registry<4> registry;
    REQUIRE(registry.register_module(module) == runtime::RegisterStatus::unknown_type_reference);
    REQUIRE(registry.size() == 0);
}
