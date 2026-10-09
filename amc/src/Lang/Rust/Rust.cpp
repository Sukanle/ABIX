#include "AMC/Lang/Rust.h"

int main(int argc, char **argv) {
    static const amc::lang::Provider provider{
        "rust",
        {
            {"backend", amc::rust::backend},
        },
    };
    return amc::lang::run_provider(argc, argv, provider);
}
