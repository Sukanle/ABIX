#include "AMC/Lang/Rust.h"

int main(int argc, char **argv) {
    static const amc::lang::Provider provider{
        "rust",
        {
          {"frontend", amc::lang::rust::run_frontend},
          {"backend", amc::lang::rust::backend},
          },
    };
    return amc::lang::run_provider(argc, argv, provider);
}
