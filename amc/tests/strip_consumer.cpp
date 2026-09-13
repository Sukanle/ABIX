// Strip-consistency consumer (AI-PM).
//
// Compiled against a generated `amc_generated.hpp`. It proves that the ABIX
// runtime contract is expressed purely through TypeID/LayoutHash metadata:
// registering the generated module and resolving a type by TypeID must keep
// working after `.abix.names` has been stripped, while name lookup — which is
// diagnostic only — is never available at runtime.
#include "amc_generated.hpp"

int main() {
    skl::abix::runtime::RuntimeRegistry<16> registry;
    if (registry.register_module(amc_generated::amc_module) != skl::abix::runtime::RuntimeRegisterStatus::ok) return 1;
    if (registry.find_by_id(amc_generated::AmcTestFoo_ABIX::type_id) == nullptr) return 2;
    if (registry.find_by_name("AmcTestFoo") != nullptr) return 3;
    return 0;
}
