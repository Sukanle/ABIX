#include "abix/self_types.cpp"

#define AMC_GENERATED_DECLARE_NATIVE_TYPE_TRAITS
#include "abix_runtime.hpp"

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
        !registry.type_of<skl::abix::runtime::ebr::Epoch>() ||
        !registry.type_of<skl::abix::runtime::ebr::RetiredNode>() ||
        !registry.type_of<skl::abix::runtime::RuntimeRegistryEntry>() ||
        !registry.type_of<skl::abix::runtime_map::Operation>() ||
        !registry.type_of<skl::abix::rcu_domain>())
        return 2;

    auto &domain = skl::abix::rcu_domain::instance();
    domain.retire(new int(7), reclaim);
    for (unsigned i = 0; i < SKL_ABIX_RCU_EPOCH_BATCH; ++i)
        domain.synchronize();
    return reclaimed == 1 ? 0 : 3;
}
