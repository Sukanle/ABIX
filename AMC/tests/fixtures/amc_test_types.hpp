#pragma once
#include <cstdint>

struct AmcTestFoo {
    int32_t x;
    double y;
};

struct AmcTestBar {
    int32_t a;
    double b;
    int32_t c;
};

enum class AmcTestColor : int32_t {
    Red = 0,
    Green = 1,
    Blue = 2,
};

int32_t amc_test_create(AmcTestFoo* out, AmcTestColor c);
int32_t amc_test_destroy(AmcTestFoo* self);
double amc_test_compute(int32_t a, double b);