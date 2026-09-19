#ifndef AMC_JSON_H
#define AMC_JSON_H

#include <string>
#include <string_view>
#include <memory>
#include <utility>
#include <vector>

#include "ljson/json.h"

namespace amc {

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

    Json();
    Json(bool value);
    Json(double value);
    Json(int value);
    Json(long long value);
    Json(unsigned value);
    Json(const char *value);
    Json(std::string value);
    Json(const Json &other);
    Json(Json &&other) noexcept;
    Json &operator=(const Json &other);
    Json &operator=(Json &&other) noexcept;
    ~Json() = default;

    static Json object();
    static Json array();

    Type type() const;
    bool is_null() const;
    bool is_object() const;
    bool is_array() const;

    // Object access.
    Json &set(std::string key, Json value);
    const Json *find(std::string_view key) const;

    // Array access.
    void push_back(Json value);
    const Array &items() const;

    bool as_bool(bool fallback = false) const;
    double as_number(double fallback = 0) const;
    std::string as_string(std::string fallback = {}) const;

    std::string dump() const;
    std::string dump_pretty(unsigned indent = 2) const;

private:
    friend bool parse_json(std::string_view text, Json &value, std::string &error);
    struct Owner {
        json_object *root = nullptr;
        json_mem_t memory{};
        bool pooled = false;
        ~Owner();
    };

    Json(std::shared_ptr<Owner> owner, json_object *node);
    static Json from_owned(json_object *node);
    static Json from_pooled(std::shared_ptr<Owner> owner, json_object *node);
    static json_object *clone_node(const Json &value);
    void replace_root(json_object *node);
    void invalidate_cache() const;
    void populate_cache() const;

    std::shared_ptr<Owner> owner_;
    json_object *node_ = nullptr;
    mutable Array items_cache_;
    mutable bool cache_valid_ = false;
};

bool parse_json(std::string_view text, Json &value, std::string &error);

}   // namespace amc

#endif
