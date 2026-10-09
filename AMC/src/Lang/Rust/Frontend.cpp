#include "AMC/Lang/Rust.h"

#include "AMC/Core/Core.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/format.h>

// Source-level Rust extractor.
//
// This is intentionally a *preliminary* frontend: it understands the subset of
// Rust that crosses an FFI boundary — `#[repr(C)]` structs/enums and
// `extern "C"` functions — and computes the C layout itself (the same layout
// rules the C++ frontend obtains from Clang). Generics, traits, lifetimes and
// bit-fields are out of scope; unsupported constructs are ignored rather than
// guessed at, per `LANGUAGE-PLUGIN.md`.
namespace amc::rust {
namespace {

constexpr uint64_t kTypeDomain = 0x54595045;   // "TYPE"

enum class Tok { Ident, Number, String, Punct, End };

struct Token {
    Tok kind = Tok::End;
    std::string text;
};

bool is_ident_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool is_ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::vector<Token> lex(const std::string &source) {
    std::vector<Token> tokens;
    const size_t n = source.size();
    size_t i = 0;
    while (i < n) {
        const char c = source[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && source[i + 1] == '/') {
            while (i < n && source[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && source[i + 1] == '*') {
            int depth = 1;
            i += 2;
            while (i < n && depth > 0) {
                if (source[i] == '/' && i + 1 < n && source[i + 1] == '*') {
                    ++depth;
                    i += 2;
                } else if (source[i] == '*' && i + 1 < n && source[i + 1] == '/') {
                    --depth;
                    i += 2;
                } else {
                    ++i;
                }
            }
            continue;
        }
        if (c == '"') {
            size_t j = i + 1;
            std::string value;
            while (j < n && source[j] != '"') {
                if (source[j] == '\\' && j + 1 < n) {
                    value += source[j + 1];
                    j += 2;
                } else {
                    value += source[j++];
                }
            }
            tokens.push_back({Tok::String, value});
            i = j < n ? j + 1 : j;
            continue;
        }
        if (c == '\'') {
            // Character literal (`'a'`, `'\n'`) vs. lifetime (`'a`).
            if (i + 2 < n && (source[i + 1] == '\\' || source[i + 2] == '\'')) {
                size_t j = i + 1;
                while (j < n && source[j] != '\'') {
                    if (source[j] == '\\') ++j;
                    ++j;
                }
                i = j < n ? j + 1 : j;
            } else {
                size_t j = i + 1;
                while (j < n && is_ident_char(source[j])) ++j;
                i = j;
            }
            continue;
        }
        if (is_ident_start(c)) {
            size_t j = i;
            while (j < n && is_ident_char(source[j])) ++j;
            tokens.push_back({Tok::Ident, source.substr(i, j - i)});
            i = j;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            size_t j = i;
            while (j < n && (is_ident_char(source[j]) || source[j] == '.')) ++j;
            tokens.push_back({Tok::Number, source.substr(i, j - i)});
            i = j;
            continue;
        }
        if (i + 1 < n) {
            const std::string two = source.substr(i, 2);
            if (two == "::" || two == "->" || two == "=>") {
                tokens.push_back({Tok::Punct, two});
                i += 2;
                continue;
            }
        }
        tokens.push_back({Tok::Punct, std::string(1, c)});
        ++i;
    }
    tokens.push_back({Tok::End, {}});
    return tokens;
}

struct RawField {
    std::string name;
    std::vector<std::string> type;
};

struct RawItem {
    enum class Kind { Record, Enum, Alias } kind = Kind::Record;
    std::string name;
    std::vector<RawField> fields;
    uint32_t align_override = 0;
    uint32_t enum_size = 4;
};

struct RawFunction {
    std::string name;
    std::vector<RawField> parameters;
    std::vector<std::string> return_type;
    bool extern_c = true;
};

struct Attrs {
    bool repr_c = false;
    bool packed = false;
    uint32_t align = 0;
    uint32_t int_size = 0;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens)
        : tokens_(std::move(tokens)) {}

    bool ok() const { return ok_; }
    const std::string &error() const { return error_; }
    std::vector<RawItem> &items() { return items_; }
    std::vector<RawFunction> &functions() { return functions_; }

    void parse() {
        while (!at_end() && ok_) parse_item();
    }

private:
    std::vector<Token> tokens_;
    size_t position_ = 0;
    bool ok_ = true;
    std::string error_;
    std::vector<RawItem> items_;
    std::vector<RawFunction> functions_;

    const Token &peek(size_t offset = 0) const {
        static const Token end{Tok::End, {}};
        return position_ + offset < tokens_.size() ? tokens_[position_ + offset] : end;
    }
    bool at_end() const { return peek().kind == Tok::End; }
    bool check(const char *text, size_t offset = 0) const { return peek(offset).text == text; }
    bool check_kind(Tok kind) const { return peek().kind == kind; }
    void advance() {
        if (!at_end()) ++position_;
    }
    bool accept(const char *text) {
        if (check(text)) {
            advance();
            return true;
        }
        return false;
    }
    bool expect(const char *text, const char *context) {
        if (accept(text)) return true;
        fail(fmt::format("expected '{}' {}", text, context));
        return false;
    }
    void fail(const std::string &message) {
        if (!ok_) return;
        ok_ = false;
        error_ = message;
    }

    void skip_balanced(const char *open, const char *close) {
        if (!accept(open)) return;
        int depth = 1;
        while (!at_end() && depth > 0) {
            if (check(open)) ++depth;
            else if (check(close)) --depth;
            advance();
        }
    }

    // Collect a type expression up to (but excluding) a top-level stop token.
    std::vector<std::string> collect_type(const std::vector<const char *> &stops) {
        std::vector<std::string> out;
        int depth = 0;
        while (!at_end()) {
            const Token &token = peek();
            if (depth == 0) {
                bool stop = false;
                for (const char *s : stops)
                    if (token.text == s) stop = true;
                if (stop) break;
            }
            if (token.kind == Tok::Punct) {
                if (token.text == "<" || token.text == "[" || token.text == "(") {
                    ++depth;
                } else if (token.text == ">" || token.text == "]" || token.text == ")") {
                    if (depth == 0) break;
                    --depth;
                }
            }
            out.push_back(token.text);
            advance();
        }
        return out;
    }

    std::string join(const std::vector<std::string> &parts) {
        std::string out;
        for (const auto &part : parts) out += part;
        return out;
    }

    Attrs parse_attributes() {
        Attrs attrs;
        while (check("#")) {
            advance();
            if (!accept("[")) {
                fail("malformed attribute");
                return attrs;
            }
            std::vector<std::string> inner;
            int depth = 1;
            while (!at_end() && depth > 0) {
                if (check("[")) ++depth;
                else if (check("]")) --depth;
                if (depth == 0) break;
                inner.push_back(peek().text);
                advance();
            }
            expect("]", "to close attribute");
            if (!inner.empty() && inner[0] == "repr") {
                attrs.repr_c = true;
                for (size_t i = 0; i < inner.size(); ++i) {
                    if (inner[i] == "packed") attrs.packed = true;
                    if (inner[i] == "align" && i + 2 < inner.size()) {
                        try {
                            attrs.align = static_cast<uint32_t>(std::stoul(inner[i + 2]));
                        } catch (...) {
                            attrs.align = 0;
                        }
                    }
                    if (inner[i] == "u8") attrs.int_size = 1;
                    else if (inner[i] == "u16" || inner[i] == "i16") attrs.int_size = 2;
                    else if (inner[i] == "u32" || inner[i] == "i32") attrs.int_size = 4;
                    else if (inner[i] == "u64" || inner[i] == "i64") attrs.int_size = 8;
                }
            }
        }
        return attrs;
    }

    void skip_visibility() {
        if (accept("pub")) {
            if (check("(")) skip_balanced("(", ")");
        }
    }

    void parse_item() {
        Attrs attrs = parse_attributes();
        if (!ok_) return;
        skip_visibility();
        while (check("unsafe") || check("const") || check("async")) advance();

        if (accept("struct")) {
            parse_struct(attrs);
            return;
        }
        if (accept("enum")) {
            parse_enum(attrs);
            return;
        }
        if (accept("type")) {
            parse_alias();
            return;
        }
        if (accept("extern")) {
            parse_extern();
            return;
        }
        if (accept("fn")) {
            parse_function();
            return;
        }
        // Unrecognised item (module, impl, use, macro, ...): skip one token.
        advance();
    }

    void parse_generics_if_any() {
        if (check("<")) skip_balanced("<", ">");
    }

    void parse_struct(const Attrs &attrs) {
        if (!check_kind(Tok::Ident)) {
            fail("struct name expected");
            return;
        }
        RawItem item;
        item.kind = RawItem::Kind::Record;
        item.name = peek().text;
        item.align_override = attrs.packed ? 1 : attrs.align;
        // Only `#[repr(...)]` types have a defined ABI; a default-layout Rust
        // struct must never be recorded (its field order/offsets are unstable).
        const bool accepted = attrs.repr_c;
        advance();
        parse_generics_if_any();
        if (accept(";")) {   // unit struct
            if (accepted) items_.push_back(std::move(item));
            return;
        }
        if (accept("(")) {   // tuple struct
            uint32_t index = 0;
            while (!at_end() && !check(")")) {
                parse_attributes();
                RawField field;
                field.name = fmt::format("field{}", index++);
                field.type = collect_type({",", ")"});
                item.fields.push_back(std::move(field));
                accept(",");
            }
            expect(")", "to close tuple struct");
            accept(";");
            if (accepted) items_.push_back(std::move(item));
            return;
        }
        if (!expect("{", "to open struct body")) return;
        while (!at_end() && !check("}")) {
            parse_attributes();
            skip_visibility();
            if (check("}")) break;
            if (!check_kind(Tok::Ident)) {
                advance();
                continue;
            }
            RawField field;
            field.name = peek().text;
            advance();
            if (!expect(":", "after field name")) return;
            field.type = collect_type({",", "}"});
            item.fields.push_back(std::move(field));
            accept(",");
        }
        expect("}", "to close struct body");
        if (accepted) items_.push_back(std::move(item));
    }

    void parse_enum(const Attrs &attrs) {
        if (!check_kind(Tok::Ident)) {
            fail("enum name expected");
            return;
        }
        RawItem item;
        item.kind = RawItem::Kind::Enum;
        item.name = peek().text;
        item.enum_size = attrs.int_size == 0 ? 4 : attrs.int_size;
        advance();
        parse_generics_if_any();
        skip_balanced("{", "}");
        if (attrs.repr_c) items_.push_back(std::move(item));
    }

    void parse_alias() {
        if (!check_kind(Tok::Ident)) {
            fail("type alias name expected");
            return;
        }
        RawItem item;
        item.kind = RawItem::Kind::Alias;
        item.name = peek().text;
        advance();
        parse_generics_if_any();
        if (!expect("=", "in type alias")) return;
        RawField underlying;
        underlying.name = "underlying";
        underlying.type = collect_type({";"});
        item.fields.push_back(std::move(underlying));
        accept(";");
        items_.push_back(std::move(item));
    }

    void parse_extern() {
        bool is_c = true;
        if (check_kind(Tok::String)) {
            is_c = peek().text == "C" || peek().text == "c";
            advance();
        }
        if (accept("{")) {
            while (!at_end() && !check("}")) {
                parse_attributes();
                skip_visibility();
                while (check("unsafe") || check("const")) advance();
                if (accept("fn")) {
                    parse_extern_function_signature(is_c);
                } else {
                    advance();
                }
            }
            expect("}", "to close extern block");
            return;
        }
        // `extern "C" fn ...` definition.
        while (check("unsafe") || check("const")) advance();
        if (accept("fn")) parse_function(is_c);
    }

    // A function declaration inside an `extern` block: `fn name(args) -> ret;`
    void parse_extern_function_signature(bool is_c) {
        RawFunction function;
        function.extern_c = is_c;
        if (check_kind(Tok::Ident)) {
            function.name = peek().text;
            advance();
        }
        parse_parameter_list(function.parameters);
        if (accept("->")) function.return_type = collect_type({";", "{"});
        accept(";");
        skip_balanced("{", "}");
        if (!function.name.empty()) functions_.push_back(std::move(function));
    }

    void parse_function(bool is_c = true) {
        RawFunction function;
        function.extern_c = is_c;
        if (check_kind(Tok::Ident)) {
            function.name = peek().text;
            advance();
        }
        parse_parameter_list(function.parameters);
        if (accept("->")) function.return_type = collect_type({";", "{"});
        accept(";");
        skip_balanced("{", "}");
        if (!function.name.empty()) functions_.push_back(std::move(function));
    }

    void parse_parameter_list(std::vector<RawField> &parameters) {
        if (!expect("(", "to open parameter list")) return;
        while (!at_end() && !check(")")) {
            parse_attributes();
            const bool reference_receiver =
                check("&") && (check("self", 1) || (check("mut", 1) && check("self", 2)));
            if (reference_receiver) {
                // Receiver (`&self`, `&mut self`) — not part of the C ABI.
                advance();
                accept("mut");
                accept("self");
                if (check(":")) {
                    advance();
                    collect_type({",", ")"});
                }
                accept(",");
                continue;
            }
            if (accept("self")) {
                accept(",");
                continue;
            }
            if (accept("mut")) {}
            std::vector<std::string> first = collect_type({":", ",", ")"});
            RawField parameter;
            if (check(":")) {
                advance();
                parameter.name = join(first);
                parameter.type = collect_type({",", ")"});
            } else {
                parameter.type = std::move(first);
            }
            parameters.push_back(std::move(parameter));
            accept(",");
        }
        expect(")", "to close parameter list");
    }
};

struct Resolved {
    amc::Hash128 id{};
    uint32_t size = 0;
    uint32_t align = 0;
};

// Builds ABIX IR from the parsed raw items, computing C layout.
class Builder {
public:
    Builder(std::vector<RawItem> items, std::vector<RawFunction> functions)
        : items_(std::move(items))
        , functions_(std::move(functions)) {
        for (size_t i = 0; i < items_.size(); ++i) by_name_.emplace(items_[i].name, i);
    }

    amc::AbiModule build(const std::vector<std::string> &wanted) {
        module_.package_name = "rust";
        for (const auto &name : wanted) {
            if (by_name_.count(name)) {
                resolve_item(name);
                continue;
            }
            for (const auto &function : functions_)
                if (function.name == name && function.extern_c) add_function(function);
        }
        return module_;
    }

    bool ok() const { return error_.empty(); }
    const std::string &error() const { return error_; }

private:
    std::vector<RawItem> items_;
    std::vector<RawFunction> functions_;
    std::unordered_map<std::string, size_t> by_name_;
    std::unordered_map<std::string, int> resolved_;
    std::unordered_map<std::string, int> primitive_ids_;
    amc::AbiModule module_;
    std::string error_;

    static uint32_t round_up(uint32_t value, uint32_t align) {
        return align <= 1 ? value : (value + align - 1) / align * align;
    }

    void set_error(const std::string &message) {
        if (error_.empty()) error_ = message;
    }

    int find_type(amc::Hash128 id) const {
        for (size_t i = 0; i < module_.types.size(); ++i)
            if (module_.types[i].id == id) return static_cast<int>(i);
        return -1;
    }

    bool primitive_spec(const std::string &spelling, amc::PrimitiveAbiKind &kind, uint32_t &width,
        amc::FloatFormat &format, bool &signedness, std::string &descriptor) {
        struct Spec {
            amc::PrimitiveAbiKind kind;
            uint32_t width;
            amc::FloatFormat format;
            bool is_signed;
            const char *descriptor;
        };
        static const std::unordered_map<std::string, Spec> specs = {
            {"bool", {amc::PrimitiveAbiKind::bool_, 1, amc::FloatFormat::none, false, "prim:bool"}},
            {"char", {amc::PrimitiveAbiKind::char32, 4, amc::FloatFormat::none, false, "prim:char32"}},
            {"i8", {amc::PrimitiveAbiKind::sint, 1, amc::FloatFormat::none, true, "prim:i1"}},
            {"i16", {amc::PrimitiveAbiKind::sint, 2, amc::FloatFormat::none, true, "prim:i2"}},
            {"i32", {amc::PrimitiveAbiKind::sint, 4, amc::FloatFormat::none, true, "prim:i4"}},
            {"i64", {amc::PrimitiveAbiKind::sint, 8, amc::FloatFormat::none, true, "prim:i8"}},
            {"i128", {amc::PrimitiveAbiKind::sint, 16, amc::FloatFormat::none, true, "prim:i16"}},
            {"isize", {amc::PrimitiveAbiKind::sint, 8, amc::FloatFormat::none, true, "prim:i8"}},
            {"u8", {amc::PrimitiveAbiKind::uint, 1, amc::FloatFormat::none, false, "prim:u1"}},
            {"u16", {amc::PrimitiveAbiKind::uint, 2, amc::FloatFormat::none, false, "prim:u2"}},
            {"u32", {amc::PrimitiveAbiKind::uint, 4, amc::FloatFormat::none, false, "prim:u4"}},
            {"u64", {amc::PrimitiveAbiKind::uint, 8, amc::FloatFormat::none, false, "prim:u8"}},
            {"u128", {amc::PrimitiveAbiKind::uint, 16, amc::FloatFormat::none, false, "prim:u16"}},
            {"usize", {amc::PrimitiveAbiKind::uint, 8, amc::FloatFormat::none, false, "prim:u8"}},
            {"f32", {amc::PrimitiveAbiKind::floating, 4, amc::FloatFormat::ieee32, false, "prim:fieee32"}},
            {"f64", {amc::PrimitiveAbiKind::floating, 8, amc::FloatFormat::ieee64, false, "prim:fieee64"}},
            {"c_char", {amc::PrimitiveAbiKind::char_signed, 1, amc::FloatFormat::none, true, "prim:char_s"}},
            {"c_schar", {amc::PrimitiveAbiKind::schar, 1, amc::FloatFormat::none, true, "prim:schar"}},
            {"c_uchar", {amc::PrimitiveAbiKind::uchar, 1, amc::FloatFormat::none, false, "prim:uchar"}},
            {"c_short", {amc::PrimitiveAbiKind::sint, 2, amc::FloatFormat::none, true, "prim:i2"}},
            {"c_ushort", {amc::PrimitiveAbiKind::uint, 2, amc::FloatFormat::none, false, "prim:u2"}},
            {"c_int", {amc::PrimitiveAbiKind::sint, 4, amc::FloatFormat::none, true, "prim:i4"}},
            {"c_uint", {amc::PrimitiveAbiKind::uint, 4, amc::FloatFormat::none, false, "prim:u4"}},
            {"c_long", {amc::PrimitiveAbiKind::sint, 8, amc::FloatFormat::none, true, "prim:i8"}},
            {"c_ulong", {amc::PrimitiveAbiKind::uint, 8, amc::FloatFormat::none, false, "prim:u8"}},
            {"c_longlong", {amc::PrimitiveAbiKind::sint, 8, amc::FloatFormat::none, true, "prim:i8"}},
            {"c_ulonglong", {amc::PrimitiveAbiKind::uint, 8, amc::FloatFormat::none, false, "prim:u8"}},
            {"c_float", {amc::PrimitiveAbiKind::floating, 4, amc::FloatFormat::ieee32, false, "prim:fieee32"}},
            {"c_double", {amc::PrimitiveAbiKind::floating, 8, amc::FloatFormat::ieee64, false, "prim:fieee64"}},
        };
        const auto it = specs.find(spelling);
        if (it == specs.end()) return false;
        kind = it->second.kind;
        width = it->second.width;
        format = it->second.format;
        signedness = it->second.is_signed;
        descriptor = it->second.descriptor;
        return true;
    }

    int add_primitive(amc::PrimitiveAbiKind kind, uint32_t width, amc::FloatFormat format, bool signedness,
        const std::string &descriptor, const std::string &spelling) {
        const auto seen = primitive_ids_.find(descriptor);
        if (seen != primitive_ids_.end()) return seen->second;
        amc::Type type;
        type.name = spelling;
        type.kind = amc::TypeKind::primitive;
        type.id = amc::hash_text(descriptor, kTypeDomain);
        type.size = width;
        type.align = width >= 16 ? 16 : (width == 0 ? 1 : width);
        type.primitive_abi = amc::primitive_abi(width, kind, format, signedness);
        const int index = static_cast<int>(module_.types.size());
        module_.types.push_back(std::move(type));
        primitive_ids_.emplace(descriptor, index);
        return index;
    }

    Resolved resolve_type(const std::vector<std::string> &tokens) {
        if (tokens.empty()) {
            set_error("empty type expression");
            return {};
        }
        // Reference / raw pointer.
        if (tokens[0] == "*" || tokens[0] == "&") {
            size_t i = 1;
            while (i < tokens.size() && (tokens[i] == "const" || tokens[i] == "mut")) ++i;
            std::string name;
            for (const auto &token : tokens) name += token;
            return add_aggregate_ref(name, 8, 8, amc::TypeKind::pointer);
        }
        // Fixed-size array `[T; N]`.
        if (tokens[0] == "[" && tokens.back() == "]") {
            size_t semicolon = tokens.size();
            for (size_t i = 1; i < tokens.size(); ++i)
                if (tokens[i] == ";") {
                    semicolon = i;
                    break;
                }
            if (semicolon >= tokens.size() - 1 || semicolon + 1 >= tokens.size() - 1) {
                set_error("malformed array type");
                return {};
            }
            std::vector<std::string> element(tokens.begin() + 1, tokens.begin() + semicolon);
            uint64_t count = 0;
            try {
                count = std::stoull(tokens[semicolon + 1]);
            } catch (...) {
                set_error("malformed array length");
                return {};
            }
            const Resolved inner = resolve_type(element);
            if (!ok()) return {};
            std::string name;
            for (const auto &token : tokens) name += token;
            amc::Type type;
            type.name = name;
            type.kind = amc::TypeKind::array;
            type.id = amc::hash_text(name, kTypeDomain);
            type.array_count = static_cast<uint32_t>(count);
            type.size = static_cast<uint32_t>(inner.size * count);
            type.align = inner.align;
            const int index = static_cast<int>(module_.types.size());
            module_.types.push_back(std::move(type));
            return {module_.types[static_cast<size_t>(index)].id, module_.types[static_cast<size_t>(index)].size,
                module_.types[static_cast<size_t>(index)].align};
        }
        // `()` -> C void (only meaningful as a return type; not emitted).
        if (tokens.size() == 2 && tokens[0] == "(" && tokens[1] == ")") {
            const int index = add_primitive(amc::PrimitiveAbiKind::void_, 0, amc::FloatFormat::none, false,
                "prim:void", "void");
            return {module_.types[static_cast<size_t>(index)].id, 0, 1};
        }
        std::string spelling;
        for (const auto &token : tokens) spelling += token;
        amc::PrimitiveAbiKind kind;
        uint32_t width = 0;
        amc::FloatFormat format = amc::FloatFormat::none;
        bool signedness = false;
        std::string descriptor;
        if (tokens.size() == 1 && primitive_spec(spelling, kind, width, format, signedness, descriptor)) {
            const int index = add_primitive(kind, width, format, signedness, descriptor, spelling);
            return {module_.types[static_cast<size_t>(index)].id, width, width >= 16 ? 16 : (width == 0 ? 1 : width)};
        }
        // Named type.
        if (!by_name_.count(spelling)) {
            set_error("unknown type: " + spelling);
            return {};
        }
        const int index = resolve_item(spelling);
        if (index < 0) return {};
        return {module_.types[static_cast<size_t>(index)].id, module_.types[static_cast<size_t>(index)].size,
            module_.types[static_cast<size_t>(index)].align};
    }

    Resolved add_aggregate_ref(const std::string &name, uint32_t size, uint32_t align, amc::TypeKind kind) {
        amc::Type type;
        type.name = name;
        type.kind = kind;
        type.id = amc::hash_text(name, kTypeDomain);
        type.size = size;
        type.align = align;
        const int index = static_cast<int>(module_.types.size());
        module_.types.push_back(std::move(type));
        return {module_.types[static_cast<size_t>(index)].id, size, align};
    }

    int resolve_item(const std::string &name) {
        const auto cached = resolved_.find(name);
        if (cached != resolved_.end()) {
            if (cached->second < 0) {
                set_error("recursive type without indirection: " + name);
                return -1;
            }
            return cached->second;
        }
        const auto found = by_name_.find(name);
        if (found == by_name_.end()) {
            set_error("unknown type: " + name);
            return -1;
        }
        const RawItem &item = items_[found->second];
        resolved_[name] = -1;   // mark in progress
        if (item.kind == RawItem::Kind::Alias) {
            return finish_alias(item);
        }
        if (item.kind == RawItem::Kind::Enum) {
            amc::Type type;
            type.name = item.name;
            type.kind = amc::TypeKind::enumeration;
            type.id = amc::hash_text(item.name, kTypeDomain);
            type.size = item.enum_size == 0 ? 4 : item.enum_size;
            type.align = type.size;
            const int index = static_cast<int>(module_.types.size());
            module_.types.push_back(std::move(type));
            resolved_[name] = index;
            return index;
        }
        return finish_record(item);
    }

    int finish_alias(const RawItem &item) {
        amc::Type type;
        type.name = item.name;
        type.kind = amc::TypeKind::alias;
        type.id = amc::hash_text(item.name, kTypeDomain);
        const int index = static_cast<int>(module_.types.size());
        type.field_begin = static_cast<uint32_t>(module_.fields.size());
        type.field_count = 1;
        module_.types.push_back(type);
        const Resolved underlying = resolve_type(item.fields.empty() ? std::vector<std::string>{} : item.fields[0].type);
        if (!ok()) return -1;
        module_.types[static_cast<size_t>(index)].size = underlying.size;
        module_.types[static_cast<size_t>(index)].align = underlying.align == 0 ? 1 : underlying.align;
        module_.fields.push_back({module_.types[static_cast<size_t>(index)].id, "underlying", underlying.id, 0, 0});
        resolved_[item.name] = index;
        return index;
    }

    int finish_record(const RawItem &item) {
        amc::Type type;
        type.name = item.name;
        type.kind = amc::TypeKind::record;
        type.id = amc::hash_text(item.name, kTypeDomain);
        const int index = static_cast<int>(module_.types.size());
        type.field_begin = static_cast<uint32_t>(module_.fields.size());
        module_.types.push_back(type);

        uint32_t offset = 0;
        uint32_t align = 1;
        for (const auto &raw : item.fields) {
            const Resolved field_type = resolve_type(raw.type);
            if (!ok()) return -1;
            const uint32_t field_align = field_type.align == 0 ? 1 : field_type.align;
            offset = round_up(offset, field_align);
            amc::Field field;
            field.owner_type = type.id;
            field.name = raw.name.empty() ? fmt::format("field{}", module_.fields.size()) : raw.name;
            field.type_id = field_type.id;
            field.offset = offset;
            offset += field_type.size;
            align = std::max(align, field_align);
            module_.fields.push_back(std::move(field));
        }
        if (item.align_override != 0) align = std::max(align, item.align_override);
        const uint32_t size = round_up(offset, align);
        auto &record = module_.types[static_cast<size_t>(index)];
        record.field_count = static_cast<uint32_t>(module_.fields.size()) - record.field_begin;
        record.size = size == 0 ? 1 : size;
        record.align = align == 0 ? 1 : align;
        std::vector<amc::Field> layout(module_.fields.begin() + record.field_begin, module_.fields.end());
        record.layout_hash = amc::layout_hash(record, layout);
        resolved_[item.name] = index;
        return index;
    }

    void add_function(const RawFunction &raw) {
        const bool void_return = raw.return_type.empty()
            || (raw.return_type.size() == 2 && raw.return_type[0] == "(" && raw.return_type[1] == ")");
        // ABIX cannot represent a zero-sized `void` return, so void-returning
        // functions are not exported by this preliminary frontend.
        if (void_return) return;
        amc::Function function;
        function.name = raw.name;
        function.calling_convention = 1;   // C
        const Resolved result = resolve_type(raw.return_type);
        if (!ok()) return;
        function.return_type = result.id;
        for (const auto &parameter : raw.parameters) {
            if (parameter.type.empty()) continue;
            if (parameter.type.size() == 2 && parameter.type[0] == "(" && parameter.type[1] == ")") continue;
            const Resolved resolved = resolve_type(parameter.type);
            if (!ok()) return;
            function.parameters.push_back({parameter.name, resolved.id, 0});
        }
        function.signature = amc::signature_hash(function);
        module_.functions.push_back(std::move(function));
    }
};

}   // namespace

int run_frontend(const char *config_path, const char *output_path) {
    amc::lang::Config config;
    std::string error;
    if (!amc::lang::config_load(config_path, "rust", config, error)) {
        fmt::print(stderr, "{}\n", error);
        return EXIT_FAILURE;
    }
    const std::filesystem::path base = std::filesystem::absolute(config_path).parent_path();

    std::vector<RawItem> items;
    std::vector<RawFunction> functions;
    std::unordered_map<std::string, bool> seen_items;
    for (const auto &file : config.files) {
        std::filesystem::path path = file;
        if (path.is_relative()) path = base / path;
        std::ifstream input(path);
        if (!input) {
            fmt::print(stderr, "cannot read {}\n", path.string());
            return EXIT_FAILURE;
        }
        std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        Parser parser(lex(source));
        parser.parse();
        if (!parser.ok()) {
            fmt::print(stderr, "{}: {}\n", path.string(), parser.error());
            return EXIT_FAILURE;
        }
        for (auto &item : parser.items())
            if (seen_items.emplace(item.name, true).second) items.push_back(std::move(item));
        for (auto &function : parser.functions()) functions.push_back(std::move(function));
    }

    Builder builder(std::move(items), std::move(functions));
    amc::AbiModule module = builder.build(config.symbols);
    if (!builder.ok()) {
        fmt::print(stderr, "{}\n", builder.error());
        return EXIT_FAILURE;
    }
    if (!amc::write_abix(module, output_path, error)) {
        fmt::print(stderr, "{}\n", error);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

}   // namespace amc::rust
