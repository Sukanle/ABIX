#include "AMC/Lang/Rust.h"

int main(int argc, char **argv) {
    static const amc::lang::Provider provider{
        "rust",
        {
            {"frontend", amc::rust::run_frontend},
            {"backend", amc::rust::backend},
        },
    };
    return amc::lang::run_provider(argc, argv, provider);
}
