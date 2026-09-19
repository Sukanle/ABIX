#include "amc_json.h"

#include <cstdint>
#include <string>

namespace amc {
namespace {

json_object *new_string(const std::string &value) {
    json_string_t string{const_cast<char *>(value.data()), {}};
    json_string_info_update(&string);
    return json_create_string(&string);
}

bool valid_input(std::string_view text, std::string &error) {
    bool in_string = false;
    bool escaped = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (!in_string) {
            if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
                i += 2;
                while (i < text.size() && text[i] != '\n' && text[i] != '\r')
                    ++i;
                continue;
            }
            if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
                i += 2;
                while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/'))
                    ++i;
                if (i + 1 >= text.size()) {
                    error = "unterminated JSON comment";
                    return false;
                }
                ++i;
                continue;
            }
            if (c == '"') in_string = true;
            continue;
        }
        if (escaped) {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            in_string = false;
        } else if (c == '\n' || c == '\r') {
            error = "raw newline in JSON string";
            return false;
        }
    }
    if (in_string) {
        error = "unterminated JSON string";
        return false;
    }
    return true;
}

}   // namespace

Json::Owner::~Owner() {
    if (pooled)
        pjson_memory_free(&memory);
    else if (root)
        json_del_object(root);
}

Json::Json()
    : owner_(std::make_shared<Owner>())
    , node_(json_create_null()) {
    owner_->root = node_;
}
Json::Json(bool value)
    : owner_(std::make_shared<Owner>())
    , node_(json_create_bool(value)) {
    owner_->root = node_;
}
Json::Json(double value)
    : owner_(std::make_shared<Owner>())
    , node_(json_create_double(value)) {
    owner_->root = node_;
}
Json::Json(int value)
    : owner_(std::make_shared<Owner>())
    , node_(json_create_lint(value)) {
    owner_->root = node_;
}
Json::Json(long long value)
    : owner_(std::make_shared<Owner>())
    , node_(json_create_lint(value)) {
    owner_->root = node_;
}
Json::Json(unsigned value)
    : owner_(std::make_shared<Owner>())
    , node_(json_create_lhex(value)) {
    owner_->root = node_;
}
Json::Json(const char *value)
    : Json(std::string(value ? value : "")) {}
Json::Json(std::string value)
    : owner_(std::make_shared<Owner>())
    , node_(new_string(value)) {
    owner_->root = node_;
}

Json::Json(std::shared_ptr<Owner> owner, json_object *node)
    : owner_(std::move(owner))
    , node_(node) {}

Json Json::from_owned(json_object *node) {
    auto owner = std::make_shared<Owner>();
    owner->root = node;
    return Json(std::move(owner), node);
}

Json Json::from_pooled(std::shared_ptr<Owner> owner, json_object *node) { return Json(std::move(owner), node); }

json_object *Json::clone_node(const Json &value) { return value.node_ ? json_deepcopy(value.node_) : nullptr; }

Json::Json(const Json &other)
    : Json(from_owned(clone_node(other))) {}

Json::Json(Json &&other) noexcept
    : owner_(std::move(other.owner_))
    , node_(other.node_)
    , items_cache_(std::move(other.items_cache_))
    , cache_valid_(other.cache_valid_) {
    other.node_ = nullptr;
    other.cache_valid_ = false;
}

Json &Json::operator=(const Json &other) {
    if (this != &other) {
        Json copy(other);
        *this = std::move(copy);
    }
    return *this;
}

Json &Json::operator=(Json &&other) noexcept {
    if (this != &other) {
        owner_ = std::move(other.owner_);
        node_ = other.node_;
        items_cache_ = std::move(other.items_cache_);
        cache_valid_ = other.cache_valid_;
        other.node_ = nullptr;
        other.cache_valid_ = false;
    }
    return *this;
}

Json Json::object() { return from_owned(json_create_object()); }
Json Json::array() { return from_owned(json_create_array()); }

Json::Type Json::type() const {
    if (!node_) return Type::null;
    switch (node_->ikey.type) {
        case JSON_BOOL:   return Type::boolean;
        case JSON_INT:
        case JSON_HEX:
        case JSON_LINT:
        case JSON_LHEX:
        case JSON_DOUBLE: return Type::number;
        case JSON_STRING: return Type::string;
        case JSON_ARRAY:  return Type::array;
        case JSON_OBJECT: return Type::object;
        default:          return Type::null;
    }
}

bool Json::is_null() const { return type() == Type::null; }
bool Json::is_object() const { return type() == Type::object; }
bool Json::is_array() const { return type() == Type::array; }

void Json::replace_root(json_object *node) {
    auto owner = std::make_shared<Owner>();
    owner->root = node;
    owner_ = std::move(owner);
    node_ = node;
    invalidate_cache();
}

void Json::invalidate_cache() const {
    items_cache_.clear();
    cache_valid_ = false;
}

void Json::populate_cache() const {
    if (cache_valid_ || !node_ || !is_array()) return;
    json_items_t items{};
    if (json_get_items(node_, &items) == 0) {
        items_cache_.reserve(items.count);
        for (uint32_t i = 0; i < items.count; ++i) {
            json_object *copy = json_deepcopy(items.items[i].json);
            if (copy) items_cache_.emplace_back(from_owned(copy));
        }
    }
    json_free_items(&items);
    cache_valid_ = true;
}

Json &Json::set(std::string key, Json value) {
    if (!node_ || !is_object()) {
        json_object *replacement = json_create_object();
        if (!replacement) return *this;
        replace_root(replacement);
    } else if (owner_->pooled) {
        json_object *copy = json_deepcopy(node_);
        if (!copy) return *this;
        replace_root(copy);
    }
    json_object *child = clone_node(value);
    if (!child) return *this;
    json_string_t jkey{const_cast<char *>(key.data()), {}};
    json_string_info_update(&jkey);
    if (json_set_key(child, &jkey) < 0 || json_replace_item_in_object(node_, child) < 0) json_del_object(child);
    invalidate_cache();
    return *this;
}

const Json *Json::find(std::string_view key) const {
    if (!node_ || !is_object()) return nullptr;
    std::string owned_key(key);
    json_object *found = json_get_object_item(node_, owned_key.c_str(), nullptr);
    if (!found) return nullptr;
    items_cache_.clear();
    json_object *copy = json_deepcopy(found);
    if (!copy) return nullptr;
    items_cache_.emplace_back(from_owned(copy));
    cache_valid_ = true;
    return &items_cache_.front();
}

void Json::push_back(Json value) {
    if (!node_ || !is_array()) {
        json_object *replacement = json_create_array();
        if (!replacement) return;
        replace_root(replacement);
    } else if (owner_->pooled) {
        json_object *copy = json_deepcopy(node_);
        if (!copy) return;
        replace_root(copy);
    }
    json_object *child = clone_node(value);
    if (child && json_add_item_to_array(node_, child) < 0) json_del_object(child);
    invalidate_cache();
}

const Json::Array &Json::items() const {
    populate_cache();
    return items_cache_;
}

bool Json::as_bool(bool fallback) const {
    if (!node_ || node_->ikey.type != JSON_BOOL) return fallback;
    return json_get_bool_value(node_);
}

double Json::as_number(double fallback) const {
    if (!node_) return fallback;
    double result = 0;
    return json_get_number_value(node_, JSON_DOUBLE, &result) < 0 ? fallback : result;
}

std::string Json::as_string(std::string fallback) const {
    json_string_t value{};
    if (!node_ || !json_get_string_value(node_, &value)) return fallback;
    return value.str ? std::string(value.str, value.info.len) : fallback;
}

std::string Json::dump() const {
    if (!node_) return "null";
    size_t length = 0;
    char *printed = json_print_unformat(node_, json_item_total_get(node_), &length, nullptr);
    if (!printed) return {};
    std::string result(printed, length);
    json_memory_free(printed);
    return result;
}

std::string Json::dump_pretty(unsigned indent) const {
    if (!node_) return "null";
    size_t length = 0;
    (void)indent;
    char *printed = json_print_format(node_, json_item_total_get(node_), &length, nullptr);
    if (!printed) return {};
    std::string result(printed, length);
    json_memory_free(printed);
    return result;
}

bool parse_json(std::string_view text, Json &value, std::string &error) {
    error.clear();
    if (!valid_input(text, error)) return false;
    std::string input(text);
    auto owner = std::make_shared<Json::Owner>();
    pjson_memory_init(&owner->memory);
    json_object *root = json_fast_parse_str(input.data(), input.size(), &owner->memory);
    if (!root) {
        pjson_memory_free(&owner->memory);
        error = "invalid JSON";
        return false;
    }
    owner->root = root;
    owner->pooled = true;
    value = Json::from_pooled(std::move(owner), root);
    return true;
}

}   // namespace amc
