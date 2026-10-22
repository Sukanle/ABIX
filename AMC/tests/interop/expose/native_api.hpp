#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// A producer whose public methods take native STL parameter types. On its own
// the frontend refuses them (`... needs a contract`); with a declared
// `[[adapt]]` mapping each native type is marshalled onto an ABIX-STD boundary
// type and the thunk takes the boundary, so a consumer sees StringView /
// SequenceView semantics. The adapter is generated and compiled, never guessed.
class Corpus {
public:
    Corpus() = default;

    uint64_t add_line(const std::string &line) {
        lines_.push_back(line);
        return lines_.size();
    }

    uint64_t measure(std::string_view text) const { return text.size(); }

    int64_t sum(std::vector<int32_t> values) const {
        int64_t total = 0;
        for (int32_t value : values)
            total += value;
        return total;
    }

    int64_t sum_span(std::span<const int32_t> values) const {
        int64_t total = 0;
        for (int32_t value : values)
            total += value;
        return total;
    }

private:
    std::vector<std::string> lines_;
};
