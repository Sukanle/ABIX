#pragma once

#include <stdint.h>

// A self-contained target for the automatic capability generation: one public
// constructor, a const getter, a mutating method, and a private field the
// consumer must not be able to reach through the ABI.
class Counter {
public:
    explicit Counter(int32_t initial)
        : value_(initial) {}

    int32_t get() const { return value_; }

    void add(int32_t amount) { value_ += amount; }

private:
    int32_t value_;
};
