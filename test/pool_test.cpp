// The slot-buffer pool in SKL/ABIX/Runtime/Search.h keeps at most one freed
// `HashIndex` slot array and hands it to the next build that wants exactly that
// capacity. These cases drive `HashIndex` directly - the same thing `Object::load`
// and `Object::reload` do with a plugin's table - because the interesting
// invariants are about buffer ownership, not about the loader.
//
// Pointer identity is used as evidence throughout, and it is sound in both
// directions: a buffer parked in the pool is owned by the pool, so no allocation
// can return it. An index that ends up with a parked buffer's address therefore
// *was* handed that buffer, and one that does not *was not*.
#include "test_common.hpp"

#include <string>
#include <thread>
#include <vector>

namespace {

using skl::abix::runtime::Entry;
using skl::abix::runtime::HashIndex;
using skl::abix::runtime::HashSlot;
using skl::abix::runtime::index_t;
using skl::abix::runtime::LookupResult;
using skl::abix::runtime::name_hash;
using skl::abix::runtime::next_pow2;
using skl::abix::runtime::Table;

constexpr uint32_t SMALL_COUNT = 64;        // 128 slots, below SKL_ABIX_HASH_POOL_MIN_SLOTS
constexpr uint32_t BIG_COUNT = 4'096;       // 8192 slots, inside the pool window
constexpr uint32_t BIGGER_COUNT = 16'384;   // 32768 slots, inside the pool window too

// A synthetic ABI table laid out exactly like a plugin's generated one, so
// HashIndex sees the same shape it sees after a real load.
struct SyntheticTable {
    std::vector<std::string> names;
    std::vector<Entry> entries;
    Table table{};

    SyntheticTable(const char *prefix, uint32_t count) {
        names.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            char buf[40];
            std::snprintf(buf, sizeof(buf), "%s_%04u", prefix, i);
            names.emplace_back(buf);
        }
        entries.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            entries.push_back({names[i].c_str(), skl::abix::dll::FnSig<int(int)>::value, 0, static_cast<uintptr_t>(i),
                name_hash(names[i].c_str()), 0});
        }
        table = {count, SKL_ABIX_TABLE_MAGIC, SKL_ABIX_TABLE_FORMAT_VERSION, 0, entries.data()};
    }
    SyntheticTable(const SyntheticTable &) = delete;
    SyntheticTable &operator=(const SyntheticTable &) = delete;

    LookupResult find(const HashIndex &idx, uint32_t i, index_t &out) const {
        const char *nm = names[i].c_str();
        return skl::abix::runtime::find_index(
            table, idx, nm, name_hash(nm), skl::abix::dll::FnSig<int(int)>::value, 0, out);
    }
};

std::vector<index_t> g_taken;   // sentinel so builds cannot be optimised away

}   // namespace

TEST_CASE("42a.a matching buffer is recycled", "[pool][runtime]") {
    log_info("Test 42a: destroy parks an in-window buffer and the next build of the same capacity takes it");

    SyntheticTable t("a", BIG_COUNT);
    HashIndex first;
    first.build(t.table);
    REQUIRE(first.valid());
    REQUIRE(first.capacity == next_pow2(BIG_COUNT * 2));
    REQUIRE(skl::abix::runtime::in_slot_pool_window(first.capacity));

    HashSlot * const buffer = first.slots;
    const uint32_t capacity = first.capacity;
    first.destroy();
    REQUIRE(!first.valid());

    HashIndex second;
    second.build(t.table);
    REQUIRE(second.slots == buffer);   // handed back by the pool, not re-allocated
    REQUIRE(second.capacity == capacity);
    log_info("recycled %u slots at %p", second.capacity, static_cast<const void *>(buffer));
    second.destroy();
}

TEST_CASE("42b.a live index never shares a buffer, and only a matching capacity is reused", "[pool][runtime]") {
    log_info("Test 42b: the pool hands a buffer to one index at a time, and only at the same capacity");

    SyntheticTable big("b", BIG_COUNT);
    SyntheticTable bigger("c", BIGGER_COUNT);

    HashIndex one;
    HashIndex two;
    one.build(big.table);
    two.build(big.table);
    REQUIRE(one.valid());
    REQUIRE(two.valid());
    // The second build finds the pool empty (whatever it was handed, `one` owns
    // it now), so it cannot be the same buffer.
    REQUIRE(one.slots != two.slots);
    HashSlot * const one_buffer = one.slots;
    HashSlot * const two_buffer = two.slots;

    one.destroy();   // parks the 8192-slot buffer

    HashIndex other;
    other.build(bigger.table);   // 32768 slots: inside the window, wrong capacity
    REQUIRE(other.slots != one_buffer);
    other.destroy();   // replaces the parked buffer (different capacity)
    two.destroy();     // 8192 slots again: this is what ends up parked

    HashIndex again;
    again.build(big.table);
    REQUIRE(again.slots == two_buffer);
    again.destroy();
}

TEST_CASE("42c.at most one buffer is retained", "[pool][runtime]") {
    log_info("Test 42c: a second release of the same capacity is freed, it does not evict the parked one");

    SyntheticTable t("d", BIG_COUNT);
    HashIndex parked;
    HashIndex other;
    parked.build(t.table);
    other.build(t.table);
    REQUIRE(parked.slots != other.slots);
    HashSlot * const kept = parked.slots;

    parked.destroy();   // parks `kept`
    other.destroy();    // same capacity: released, must not evict `kept`

    HashIndex third;
    third.build(t.table);
    REQUIRE(third.slots == kept);
    third.destroy();
}

TEST_CASE("42d.the window decides what is retained", "[pool][runtime]") {
    log_info("Test 42d: a table below the pool floor is released, not parked");

    SyntheticTable small("e", SMALL_COUNT);
    SyntheticTable big("f", BIG_COUNT);

    // Fill the single slot first, so "was the small buffer parked?" becomes
    // observable: offering a *different* capacity replaces the parked buffer, so
    // if the small one had been parked this build would miss.
    HashIndex warm;
    warm.build(big.table);
    HashSlot * const parked_buffer = warm.slots;
    warm.destroy();

    HashIndex tiny;
    tiny.build(small.table);
    REQUIRE(tiny.valid());
    REQUIRE(!skl::abix::runtime::in_slot_pool_window(tiny.capacity));
    tiny.destroy();

    HashIndex again;
    again.build(big.table);
    REQUIRE(again.slots == parked_buffer);
    again.destroy();
}

TEST_CASE("42e.a recycled buffer serves no stale entry", "[pool][runtime]") {
    log_info("Test 42e: entries of the table that owned the buffer are gone after it is reused");

    SyntheticTable before("old", BIG_COUNT);
    SyntheticTable after("new", BIG_COUNT);

    HashIndex reference;   // a fresh index over `after`, to compare results against
    reference.build(after.table);
    REQUIRE(reference.valid());

    HashIndex recycled;
    recycled.build(before.table);
    HashSlot * const buffer = recycled.slots;
    REQUIRE(buffer != reference.slots);
    recycled.destroy();

    HashIndex reused;
    reused.build(after.table);
    REQUIRE(reused.slots == buffer);   // the reuse really happened
    REQUIRE(reused.capacity == reference.capacity);

    index_t out = ~index_t{0};
    for (uint32_t i = 0; i < after.table.count; ++i) {
        index_t reference_out = ~index_t{0};
        REQUIRE(after.find(reference, i, reference_out) == LookupResult::ok);
        REQUIRE(reference_out == i);

        REQUIRE(after.find(reused, i, out) == LookupResult::ok);
        REQUIRE(out == reference_out);
        g_taken.push_back(out);
    }

    // The previous table's names must be gone from the recycled slots.
    for (uint32_t i = 0; i < before.table.count; ++i) {
        const char *nm = before.names[i].c_str();
        out = ~index_t{0};
        REQUIRE(skl::abix::runtime::find_index(
                    after.table, reused, nm, name_hash(nm), skl::abix::dll::FnSig<int(int)>::value, 0, out)
                == LookupResult::not_found);
    }
    log_info("recycled %u slots: %u new entries resolved, %u stale names rejected", reused.capacity, after.table.count,
        before.table.count);
    reused.destroy();
    reference.destroy();
}

TEST_CASE("42f.concurrent builds share the pool safely", "[pool][threads]") {
    log_info("Test 42f: threads building indices of mixed capacities get correct results");

    SyntheticTable big("g", BIG_COUNT);
    SyntheticTable bigger("h", BIGGER_COUNT);

    std::atomic<int> wrong{0};
    std::vector<std::thread> threads;
    threads.reserve(8);
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&big, &bigger, t, &wrong] {
            // Mixed capacities on purpose: half the threads ask for a different
            // capacity than the other half, so the single slot is continuously
            // taken and replaced.
            const SyntheticTable &tbl = (t % 2 == 0) ? big : bigger;
            for (int round = 0; round < 200; ++round) {
                HashIndex idx;
                idx.build(tbl.table);
                if (!idx.valid()) {
                    ++wrong;
                    return;
                }
                for (uint32_t i = 0; i < tbl.table.count; i += 61) {
                    index_t out = ~index_t{0};
                    if (tbl.find(idx, i, out) != LookupResult::ok || out != i) ++wrong;
                }
                idx.destroy();
            }
        });
    }
    for (auto &thread : threads)
        thread.join();

    REQUIRE(wrong.load() == 0);
    log_info("8 threads x 200 build/lookup/destroy rounds: 0 wrong results");
}

TEST_CASE("42g.the pool seed does not break the linear fallback", "[pool][runtime]") {
    log_info("Test 42g: a below-threshold table has no index, and lookups still work");

    // Small tables never build an index (SKL_ABIX_HASH_THRESHOLD), so find_index
    // must fall back to the linear scan over a table the pool never saw.
    SyntheticTable small("i", SMALL_COUNT);
    HashIndex empty;
    REQUIRE(!empty.valid());

    index_t out = ~index_t{0};
    REQUIRE(small.find(empty, 7, out) == LookupResult::ok);
    REQUIRE(out == 7);

    const char *missing = "i_9999";
    REQUIRE(skl::abix::runtime::find_index(
                small.table, empty, missing, name_hash(missing), skl::abix::dll::FnSig<int(int)>::value, 0, out)
            == LookupResult::not_found);
}
