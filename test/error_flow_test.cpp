#include "test_common.hpp"

#include "SKL/ABIX/Error/Mapping.h"

#include <string>

namespace {
using skl::abix::dll::last_error;
}

TEST_CASE("error_flow.load_reports_explicitly", "[error][dll]") {
    using skl::abix::dll::CallError;
    using skl::abix::dll::Object;

    Object lib;
    const std::string path = dll_path("math_dll");

    last_error() = CallError::not_found;
    CallError error = CallError::invalid;
    REQUIRE(lib.load(path.c_str(), error));
    CHECK(error == CallError::none);
    CHECK(last_error() == CallError::not_found);

    CHECK(lib.unload());
    CHECK(last_error() == CallError::none);

    // A failure through the explicit form must not disturb last_error().
    last_error() = CallError::not_found;
    error = CallError::none;
    CHECK(!lib.load("/nonexistent/abix/error_flow_test", error));
    CHECK(error == CallError::load_failed);
    CHECK(last_error() == CallError::not_found);
}

TEST_CASE("error_flow.reload_reports_explicitly", "[error][dll]") {
    using skl::abix::dll::CallError;
    using skl::abix::dll::Object;

    Object lib;
    const std::string path = dll_path("math_dll");
    CallError error = CallError::invalid;

    REQUIRE(lib.load(path.c_str(), error));
    REQUIRE(error == CallError::none);

    last_error() = CallError::invalid;
    REQUIRE(lib.reload(path.c_str(), error));
    CHECK(error == CallError::none);
    CHECK(last_error() == CallError::invalid);

    // A failed reload leaves the previous image in place.
    error = CallError::none;
    CHECK(!lib.reload("/nonexistent/abix/error_flow_test", error));
    CHECK(error == CallError::load_failed);
    CHECK(lib.is_loaded());
}

TEST_CASE("error_flow.resolve_reports_explicitly", "[error][dll]") {
    using skl::abix::dll::CallError;
    using skl::abix::dll::Function;
    using skl::abix::dll::Object;

    Object lib;
    const std::string path = dll_path("math_dll");
    CallError error = CallError::invalid;
    REQUIRE(lib.load(path.c_str(), error));

    last_error() = CallError::invalid;
    Function<int(int, int)> add;
    CHECK(add.try_resolve(lib, "add") == CallError::none);
    CHECK(last_error() == CallError::invalid);
    CHECK(add.valid());

    CHECK(add.try_resolve(lib, "missing") == CallError::not_found);
    CHECK(last_error() == CallError::invalid);
    CHECK(!add.valid());

    Function<double(double, double)> wrong_signature;
    CHECK(wrong_signature.try_resolve(lib, "add") == CallError::sig_mismatch);
    CHECK(last_error() == CallError::invalid);
    CHECK(!wrong_signature.valid());

    // The compatibility form still writes last_error().
    Function<int(int, int)> compat;
    compat.resolve(lib, "missing");
    CHECK(last_error() == CallError::not_found);
}

TEST_CASE("error_flow.unload_is_explicit_and_codes_survive", "[error][lifecycle]") {
    using namespace skl::abix;
    using skl::abix::dll::CallError;
    using skl::abix::dll::Function;
    using skl::abix::dll::Object;

    const ErrorCode saved = to_error_code(CallError::stale_handle);
    CHECK(saved.domain == call_domain);
    CHECK(saved.value == static_cast<int32_t>(CallError::stale_handle));
    CHECK(is_canonical(saved));
    CHECK(!is_success(saved));

    {
        Object lib;
        const std::string path = dll_path("math_dll");
        CallError error = CallError::invalid;
        REQUIRE(lib.load(path.c_str(), error));
        REQUIRE(error == CallError::none);

        Function<int(int, int)> add;
        REQUIRE(add.try_resolve(lib, "add") == CallError::none);

        last_error() = CallError::none;
        CHECK(!lib.unload(error));
        CHECK(error == CallError::stale_handle);
        CHECK(last_error() == CallError::none);

        // The handle keeps the module alive; the saved code is already independent
        // of it and stays comparable after the object goes away.
        CHECK(add(2, 3) == 5);
    }

    CHECK(saved.domain == call_domain);
    CHECK(saved.value == static_cast<int32_t>(CallError::stale_handle));
    CHECK(is_canonical(saved));
    CHECK(!is_success(saved));
}
