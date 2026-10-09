#include "AMC/Util/Json.h"

#include <fmt/format.h>

namespace amc::util {

std::string escape_json(const std::string &value) {
    std::string output;
    for (unsigned char c : value) {
        switch (c) {
            case '"':  output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (c < 0x20)
                    output += fmt::format("\\u{:04x}", static_cast<unsigned>(c));
                else
                    output += static_cast<char>(c);
        }
    }
    return output;
}

void json_string(std::string &output, const std::string &value) {
    output += '"';
    output += escape_json(value);
    output += '"';
}

void indent(std::string &output, unsigned depth) { output += std::string(depth * 2, ' '); }

}   // namespace amc::util
