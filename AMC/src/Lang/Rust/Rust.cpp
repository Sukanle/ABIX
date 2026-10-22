#include "AMC/Lang/Rust.h"

int main(int argc, char **argv) {
    static const amc::lang::Provider provider{
        "rust",
        {
          {"frontend", amc::lang::rust::run_frontend},
          {"backend", amc::lang::rust::backend},
          },
        "0.2.0",
        {"ownership", "error", "target"},
    };
    return amc::lang::run_provider(argc, argv, provider);
}
