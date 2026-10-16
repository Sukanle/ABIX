#pragma once

#include <cstdint>

// A self-contained target for the automatic capability generation: one public
// constructor, a const getter, a mutating method, and a private field the
// consumer must not be able to reach through the ABI.
class Counter {
public:
    explicit Counter(std::int32_t initial)
        : value_(initial) {}

    std::int32_t get() const { return value_; }

    void add(std::int32_t amount) { value_ += amount; }

private:
    std::int32_t value_;
};
