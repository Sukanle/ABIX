#include "test_common.hpp"

#include "SKL/ABIX/Util/Atomic.h"

#include <stdint.h>

using skl::abix::util::cas_relaxed;
using skl::abix::util::compare_exchange;
using skl::abix::util::dec_relaxed;
using skl::abix::util::exchange_acq_rel;
using skl::abix::util::inc_acq_rel;
using skl::abix::util::inc_relaxed;
using skl::abix::util::is_always_lock_free;
using skl::abix::util::is_atomic_cas_v;
using skl::abix::util::is_atomic_integer_v;
using skl::abix::util::is_atomic_scalar_v;
using skl::abix::util::load_acquire;
using skl::abix::util::load_relaxed;
using skl::abix::util::store_relaxed;
using skl::abix::util::store_release;

// Type constraints: integral (excluding bool) and object pointers for
// load/store/exchange; integers (excluding bool) for read-modify-write; the
// scalar set for compare-exchange. Floating point is excluded everywhere.
static_assert(is_atomic_scalar_v<uint32_t>, "uint32 is a scalar");
static_assert(is_atomic_scalar_v<uint64_t>, "uint64 is a scalar");
static_assert(is_atomic_scalar_v<int *>, "pointer is a scalar");
static_assert(!is_atomic_scalar_v<float>, "float is not a scalar");
static_assert(!is_atomic_scalar_v<double>, "double is not a scalar");
static_assert(!is_atomic_scalar_v<bool>, "bool is not a scalar");
static_assert(is_atomic_integer_v<uint32_t>, "uint32 supports RMW");
static_assert(!is_atomic_integer_v<int *>, "pointers do not support RMW");
static_assert(!is_atomic_integer_v<bool>, "bool does not support RMW");
static_assert(is_atomic_cas_v<uint64_t>, "uint64 supports CAS");
static_assert(is_atomic_cas_v<int *>, "pointers support CAS");
static_assert(!is_atomic_cas_v<double>, "double does not support CAS");

// The common ABIX widths must be lock-free.
static_assert(is_always_lock_free<uint32_t>, "uint32 must be lock-free");
static_assert(is_always_lock_free<uint64_t>, "uint64 must be lock-free");
static_assert(is_always_lock_free<void *>, "pointers must be lock-free");

TEST_CASE("36.atomic load/store", "[atomic]") {
    uint64_t value = 0;
    store_release(&value, uint64_t{0x1122334455667788ULL});
    REQUIRE(load_acquire(&value) == 0x1122334455667788ULL);
    store_relaxed(&value, uint64_t{1});
    REQUIRE(load_relaxed(&value) == 1ULL);

    int target = 5;
    int *slot = nullptr;
    store_release(&slot, &target);
    REQUIRE(load_acquire(&slot) == &target);
}

TEST_CASE("37.atomic read-modify-write returns the new value", "[atomic]") {
    uint32_t counter = 0;
    REQUIRE(inc_relaxed(&counter) == 1u);
    REQUIRE(inc_acq_rel(&counter) == 2u);
    REQUIRE(dec_relaxed(&counter) == 1u);

    uint32_t slot = 7;
    REQUIRE(exchange_acq_rel(&slot, uint32_t{9}) == 7u);
    REQUIRE(slot == 9u);

    int target = 1;
    int *ptr = nullptr;
    REQUIRE(exchange_acq_rel(&ptr, &target) == nullptr);
    REQUIRE(ptr == &target);
}

TEST_CASE("38.atomic compare-exchange updates expected on failure", "[atomic]") {
    uint32_t value = 5;
    uint32_t expected = 0;   // stale
    REQUIRE_FALSE(compare_exchange(&value, expected, uint32_t{9}));
    REQUIRE(expected == 5u);   // observed value written back for a retry
    REQUIRE(value == 5u);

    REQUIRE(compare_exchange(&value, expected, uint32_t{9}));
    REQUIRE(value == 9u);

    REQUIRE_FALSE(cas_relaxed(&value, uint32_t{1}, uint32_t{2}));
    REQUIRE(cas_relaxed(&value, uint32_t{9}, uint32_t{2}));
    REQUIRE(value == 2u);
}
