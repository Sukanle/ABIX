#pragma once

namespace amc_m8 {

using Count = unsigned;

struct Base {
    int base_value;
};

template <typename T>
struct Box : Base {
    T value;
    unsigned bits : 3;
private:
    int hidden;
};

struct IntBox : Box<int> {};

__attribute__((cdecl)) int cdecl_fn(int value);

}
