#include "amc_json.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace amc {
namespace {

void escape_into(std::string &out, std::string_view value) {
    for (unsigned char c : value) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
}

void append_utf8(std::string &out, uint32_t code) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

struct Parser {
    std::string_view text;
    size_t pos = 0;
    std::string error;
    int depth = 0;

    bool fail(const std::string &message) {
        if (error.empty()) error = message + " at offset " + std::to_string(pos);
        return false;
    }

    void skip_ws() {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r'))
            ++pos;
    }

    bool consume(std::string_view token) {
        if (text.compare(pos, token.size(), token) != 0) return fail("invalid literal");
        pos += token.size();
        return true;
    }

    bool parse_hex4(uint32_t &value) {
        if (pos + 4 > text.size()) return fail("truncated \\u escape");
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text[pos++];
            value <<= 4;
            if (c >= '0' && c <= '9')
                value |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                value |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value |= static_cast<uint32_t>(c - 'A' + 10);
            else
                return fail("invalid hex digit in \\u escape");
        }
        return true;
    }

    bool parse_string(std::string &out) {
        if (pos >= text.size() || text[pos] != '"') return fail("expected string");
        ++pos;
        out.clear();
        while (pos < text.size()) {
            const unsigned char c = static_cast<unsigned char>(text[pos++]);
            if (c == '"') return true;
            if (c == '\\') {
                if (pos >= text.size()) return fail("truncated escape");
                const char e = text[pos++];
                switch (e) {
                    case '"':  out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/'; break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'u':  {
                        uint32_t code = 0;
                        if (!parse_hex4(code)) return false;
                        if (code >= 0xD800
                            && code <= 0xDBFF
                            && pos + 2 <= text.size()
                            && text[pos] == '\\'
                            && text[pos + 1] == 'u') {
                            pos += 2;
                            uint32_t low = 0;
                            if (!parse_hex4(low)) return false;
                            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        }
                        append_utf8(out, code);
                        break;
                    }
                    default: return fail("invalid escape");
                }
            } else {
                out += static_cast<char>(c);
            }
        }
        return fail("unterminated string");
    }

    bool parse_number(Json &out) {
        const size_t begin = pos;
        if (pos < text.size() && text[pos] == '-') ++pos;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
            ++pos;
        if (pos < text.size() && text[pos] == '.') {
            ++pos;
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
                ++pos;
        }
        if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
            ++pos;
            if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) ++pos;
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
                ++pos;
        }
        if (pos == begin) return fail("invalid number");
        const std::string slice(text.substr(begin, pos - begin));
        char *end = nullptr;
        const double value = std::strtod(slice.c_str(), &end);
        if (end == nullptr || *end != '\0') return fail("invalid number");
        out = Json(value);
        return true;
    }

    bool parse_array(Json &out) {
        ++pos;   // '['
        out = Json::array();
        skip_ws();
        if (pos < text.size() && text[pos] == ']') {
            ++pos;
            return true;
        }
        while (true) {
            Json element;
            if (!parse_value(element)) return false;
            out.push_back(std::move(element));
            skip_ws();
            if (pos >= text.size()) return fail("unterminated array");
            if (text[pos] == ',') {
                ++pos;
                skip_ws();
                continue;
            }
            if (text[pos] == ']') {
                ++pos;
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    bool parse_object(Json &out) {
        ++pos;   // '{'
        out = Json::object();
        skip_ws();
        if (pos < text.size() && text[pos] == '}') {
            ++pos;
            return true;
        }
        while (true) {
            skip_ws();
            std::string key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (pos >= text.size() || text[pos] != ':') return fail("expected ':'");
            ++pos;
            Json value;
            if (!parse_value(value)) return false;
            out.set(std::move(key), std::move(value));
            skip_ws();
            if (pos >= text.size()) return fail("unterminated object");
            if (text[pos] == ',') {
                ++pos;
                continue;
            }
            if (text[pos] == '}') {
                ++pos;
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    bool parse_value(Json &out) {
        if (++depth > 128) return fail("nesting too deep");
        skip_ws();
        if (pos >= text.size()) return fail("unexpected end of input");
        const char c = text[pos];
        bool ok = false;
        if (c == '{')
            ok = parse_object(out);
        else if (c == '[')
            ok = parse_array(out);
        else if (c == '"') {
            std::string s;
            ok = parse_string(s);
            if (ok) out = Json(std::move(s));
        } else if (c == 't') {
            ok = consume("true");
            if (ok) out = Json(true);
        } else if (c == 'f') {
            ok = consume("false");
            if (ok) out = Json(false);
        } else if (c == 'n') {
            ok = consume("null");
            if (ok) out = Json();
        } else if (c == '-' || (c >= '0' && c <= '9'))
            ok = parse_number(out);
        else
            ok = fail("unexpected character");
        --depth;
        return ok;
    }
};

}   // namespace

Json &Json::set(std::string key, Json value) {
    type_ = Type::object;
    for (auto &member : object_) {
        if (member.first == key) {
            member.second = std::move(value);
            return *this;
        }
    }
    object_.emplace_back(std::move(key), std::move(value));
    return *this;
}

const Json *Json::find(std::string_view key) const {
    for (const auto &member : object_)
        if (member.first == key) return &member.second;
    return nullptr;
}

void Json::push_back(Json value) {
    type_ = Type::array;
    array_.push_back(std::move(value));
}

void Json::dump_to(std::string &out, unsigned indent, unsigned depth) const {
    const auto newline = [&](unsigned level) {
        if (indent == 0) return;
        out += '\n';
        out.append(static_cast<size_t>(level) * indent, ' ');
    };
    switch (type_) {
        case Type::null:    out += "null"; break;
        case Type::boolean: out += boolean_ ? "true" : "false"; break;
        case Type::number:
            if (std::isfinite(number_) && number_ == std::floor(number_) && std::fabs(number_) < 1e15) {
                out += std::to_string(static_cast<long long>(number_));
            } else {
                std::ostringstream stream;
                stream << number_;
                out += stream.str();
            }
            break;
        case Type::string:
            out += '"';
            escape_into(out, string_);
            out += '"';
            break;
        case Type::array:
            out += '[';
            for (size_t i = 0; i < array_.size(); ++i) {
                if (i) out += ',';
                newline(depth + 1);
                array_[i].dump_to(out, indent, depth + 1);
            }
            if (!array_.empty()) newline(depth);
            out += ']';
            break;
        case Type::object:
            out += '{';
            for (size_t i = 0; i < object_.size(); ++i) {
                if (i) out += ',';
                newline(depth + 1);
                out += '"';
                escape_into(out, object_[i].first);
                out += indent == 0 ? "\":" : "\": ";
                object_[i].second.dump_to(out, indent, depth + 1);
            }
            if (!object_.empty()) newline(depth);
            out += '}';
            break;
    }
}

std::string Json::dump() const {
    std::string out;
    dump_to(out, 0, 0);
    return out;
}

std::string Json::dump_pretty(unsigned indent) const {
    std::string out;
    dump_to(out, indent, 0);
    return out;
}

bool parse_json(std::string_view text, Json &value, std::string &error) {
    Parser parser;
    parser.text = text;
    if (!parser.parse_value(value)) {
        error = parser.error;
        return false;
    }
    parser.skip_ws();
    if (parser.pos != text.size()) {
        error = "trailing characters at offset " + std::to_string(parser.pos);
        return false;
    }
    return true;
}

}   // namespace amc
