#include "../core/amc_core.h"
#include <cassert>
#include <cstdio>

int main() {
    amc::AbiModule m;
    m.package_name = "test";
    amc::Type t;
    t.name = "i32";
    t.id = amc::hash_text(t.name, 0x54595045);
    t.size = 4;
    t.align = 4;
    m.types.push_back(t);
    amc::Function f;
    f.name = "f";
    f.return_type = t.id;
    f.signature = amc::signature_hash(f);
    m.functions.push_back(f);
    std::string error;
    assert(amc::write_abix(m, "amc_core_test.abix", error));
    amc::AbiModule loaded;
    assert(amc::read_abix("amc_core_test.abix", loaded, error));
    assert(loaded.types.size() == 1);
    assert(loaded.functions.size() == 1);
    std::remove("amc_core_test.abix");
}
