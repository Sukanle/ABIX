#ifndef AMC_JSON_H
#define AMC_JSON_H

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace amc {

// Minimal JSON value + parser/serializer.
//
// ABIX's tooling deliberately avoids a heavy JSON dependency: the AMC CLI and
// the MCP server only need to parse small request objects and emit structured
// responses. This is a complete, self-contained implementation for that use.

class Json {
public:
    using Array = std::vector<Json>;
    using Member = std::pair<std::string, Json>;
    using Object = std::vector<Member>;

    enum class Type {
        null,
        boolean,
        number,
        string,
        array,
        object
    };

    Json() = default;
    Json(bool value)
        : type_(Type::boolean)
        , boolean_(value) {}
    Json(double value)
        : type_(Type::number)
        , number_(value) {}
    Json(int value)
        : type_(Type::number)
        , number_(value) {}
    Json(long long value)
        : type_(Type::number)
        , number_(static_cast<double>(value)) {}
    Json(unsigned value)
        : type_(Type::number)
        , number_(value) {}
    Json(const char *value)
        : type_(Type::string)
        , string_(value) {}
    Json(std::string value)
        : type_(Type::string)
        , string_(std::move(value)) {}

    static Json object() {
        Json value;
        value.type_ = Type::object;
        return value;
    }
    static Json array() {
        Json value;
        value.type_ = Type::array;
        return value;
    }

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::null; }
    bool is_object() const { return type_ == Type::object; }
    bool is_array() const { return type_ == Type::array; }

    // Object access.
    Json &set(std::string key, Json value);
    const Json *find(std::string_view key) const;

    // Array access.
    void push_back(Json value);
    const Array &items() const { return array_; }

    bool as_bool(bool fallback = false) const { return type_ == Type::boolean ? boolean_ : fallback; }
    double as_number(double fallback = 0) const { return type_ == Type::number ? number_ : fallback; }
    std::string as_string(std::string fallback = {}) const {
        return type_ == Type::string ? string_ : std::move(fallback);
    }

    std::string dump() const;
    std::string dump_pretty(unsigned indent = 2) const;

private:
    void dump_to(std::string &out, unsigned indent, unsigned depth) const;

    Type type_ = Type::null;
    bool boolean_ = false;
    double number_ = 0;
    std::string string_;
    Array array_;
    Object object_;
};

bool parse_json(std::string_view text, Json &value, std::string &error);

}   // namespace amc

#endif
