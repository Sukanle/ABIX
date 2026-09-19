#include "abix/self_types.cpp"

#define AMC_GENERATED_DECLARE_NATIVE_TYPE_TRAITS
#include "abix_self_metadata.hpp"

namespace {
int reclaimed = 0;

void reclaim(void *object) noexcept {
    ++reclaimed;
    delete static_cast<int *>(object);
}
}  // namespace

int main() {
    skl::abix::runtime::RuntimeRegistry<64> registry;
    if (registry.register_module(amc_generated::amc_module) !=
        skl::abix::runtime::RuntimeRegisterStatus::ok)
        return 1;
    if (!registry.type_of<skl::abix::model::TypeDesc>() ||
        !registry.type_of<skl::abix::model::TypeInfo>() ||
        !registry.type_of<skl::abix::runtime::ebr::Epoch>() ||
        !registry.type_of<skl::abix::runtime::ebr::RetiredNode>() ||
        !registry.type_of<skl::abix::runtime::RegistryEntry>() ||
        !registry.type_of<skl::abix::runtime::RuntimeRegistryEntry>() ||
        !registry.type_of<skl::abix::runtime_map::Operation>() ||
        !registry.type_of<skl::abix::rcu_domain>())
        return 2;

    const auto *entry = registry.type_of<skl::abix::runtime::RegistryEntry>();
    if (registry.find_by_id(entry->descriptor->type_id) != entry ||
        registry.canonical().find_by_id(entry->canonical->id) == nullptr)
        return 3;

    const auto matches_native_layout = [](const auto *registered, size_t size, size_t align) {
        return registered->descriptor->size == size && registered->descriptor->align == align;
    };
    if (!matches_native_layout(registry.type_of<skl::abix::model::TypeInfo>(),
                               sizeof(skl::abix::model::TypeInfo), alignof(skl::abix::model::TypeInfo)) ||
        !matches_native_layout(entry, sizeof(skl::abix::runtime::RegistryEntry),
                               alignof(skl::abix::runtime::RegistryEntry)) ||
        !matches_native_layout(registry.type_of<skl::abix::runtime::ebr::Epoch>(),
                               sizeof(skl::abix::runtime::ebr::Epoch), alignof(skl::abix::runtime::ebr::Epoch)) ||
        !matches_native_layout(registry.type_of<skl::abix::runtime::ebr::RetiredNode>(),
                               sizeof(skl::abix::runtime::ebr::RetiredNode), alignof(skl::abix::runtime::ebr::RetiredNode)))
        return 4;

    auto &domain = skl::abix::rcu_domain::instance();
    domain.retire(new int(7), reclaim);
    for (unsigned i = 0; i < SKL_ABIX_RCU_EPOCH_BATCH; ++i)
        domain.synchronize();
    return reclaimed == 1 ? 0 : 5;
}