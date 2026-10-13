#include "AMC/Lang/Zig.h"

int main(int argc, char **argv) {
    static const amc::lang::Provider provider{
        "zig",
        {
          {"backend", amc::lang::zig::backend},
          },
    };
    return amc::lang::run_provider(argc, argv, provider);
}
