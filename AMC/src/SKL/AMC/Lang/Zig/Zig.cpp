#include "SKL/AMC/Lang/Zig.h"

int main(int argc, char **argv) {
    static const amc::lang::Provider provider{
        "zig",
        {
          {"backend", amc::lang::zig::backend},
          },
        "0.2.0",
        {"target"},
    };
    return amc::lang::run_provider(argc, argv, provider);
}
