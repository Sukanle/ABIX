#pragma once

#include <stdint.h>

#include "SKL/ABIX/STD/Result.h"
#include "SKL/ABIX/STD/Span.h"
#include "SKL/ABIX/STD/String.h"

// A producer whose public methods speak ABIX-STD boundary types directly.
// `[[expose]]` forwards them and records their contracts, so a consumer sees
// SequenceView / StringView / Result semantics, not just a byte layout.
class Metrics {
public:
    Metrics() = default;

    int32_t sum(abix_span_i32 values) const {
        int32_t total = 0;
        for (uint64_t i = 0; i < values.len; ++i)
            total += values.data[i];
        return total;
    }

    uint64_t text_length(abix_str_utf8 text) const { return text.len; }

    abix_result_i32 divide(int32_t a, int32_t b) const {
        abix_result_i32 result{};
        if (b == 0) {
            result.status = 1;
            result.value = 0;
        } else {
            result.status = 0;
            result.value = a / b;
        }
        return result;
    }
};
