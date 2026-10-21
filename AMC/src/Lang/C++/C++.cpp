#include "AMC/Lang/C++.h"

#include <fmt/format.h>

int main(int argc, char **argv) {
#ifdef __APPLE__
    if (!amc::lang::cpp::kClangResourceDir[0]) {
        fmt::print(stderr,
            "warning: ABIX_CLANG_RESOURCE_DIR not set at build time; "
            "built-in headers (stdarg.h, etc.) may not be found.\n");
    }
#endif

    static const amc::lang::Provider provider{
        "cpp",
        {
          {"frontend", amc::lang::cpp::run_frontend},
          {"backend", amc::lang::cpp::backend},
          },
        "0.2.0",
        {"templates", "interfaces"},
    };
    return amc::lang::run_provider(argc, argv, provider);
}
