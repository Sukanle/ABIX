#include "AMC/Lang/C++.h"

int main(int argc, char **argv) {
#ifdef __APPLE__
    if (!amc::cpp::kClangResourceDir[0]) {
        fmt::print(stderr,
            "warning: ABIX_CLANG_RESOURCE_DIR not set at build time; "
            "built-in headers (stdarg.h, etc.) may not be found.\n");
    }
#endif

    static const amc::lang::Provider provider{
        "cpp",
        {
            {"frontend", amc::cpp::run_frontend},
            {"backend", amc::cpp::backend},
        },
    };
    return amc::lang::run_provider(argc, argv, provider);
}
