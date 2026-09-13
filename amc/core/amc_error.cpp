#include "amc_error.h"

#include <cctype>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <utility>

namespace amc {

const char *error_category_name(ErrorCategory category) {
    switch (category) {
        case ErrorCategory::usage:         return "usage";
        case ErrorCategory::config:        return "config";
        case ErrorCategory::io:            return "io";
        case ErrorCategory::provider:      return "provider";
        case ErrorCategory::format:        return "format";
        case ErrorCategory::validation:    return "validation";
        case ErrorCategory::compatibility: return "compatibility";
        case ErrorCategory::internal:      return "internal";
    }
    return "internal";
}

std::string error_code(ErrorCategory category) {
    std::string name = error_category_name(category);
    for (char &c : name)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return "AMC-" + name;
}

Error make_error(ErrorCategory category, std::string stage, std::string message, std::string file, std::string detail) {
    Error error;
    error.category = category;
    error.stage = std::move(stage);
    error.message = std::move(message);
    error.file = std::move(file);
    error.detail = std::move(detail);
    return error;
}

std::string json_escape(std::string_view value) {
    std::ostringstream output;
    for (unsigned char c : value) {
        switch (c) {
            case '"':  output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (c < 0x20)
                    output
                        << "\\u"
                        << std::hex
                        << std::setw(4)
                        << std::setfill('0')
                        << static_cast<unsigned>(c)
                        << std::dec
                        << std::setfill(' ');
                else
                    output << c;
        }
    }
    return output.str();
}

namespace {
void json_optional(std::ostream &output, const char *key, const std::string &value) {
    output << ",\"" << key << "\":";
    if (value.empty())
        output << "null";
    else
        output << '"' << json_escape(value) << '"';
}
}   // namespace

std::string error_to_json(const Error &error) {
    std::ostringstream output;
    output
        << "{\"schema\":\"abix.error/1\",\"error\":{"
        << "\"code\":\""
        << error_code(error.category)
        << '"'
        << ",\"category\":\""
        << error_category_name(error.category)
        << '"';
    json_optional(output, "stage", error.stage);
    json_optional(output, "message", error.message);
    json_optional(output, "file", error.file);
    json_optional(output, "detail", error.detail);
    output << "}}";
    return output.str();
}

std::string error_to_text(const Error &error) {
    std::ostringstream output;
    output << "amc: " << error_category_name(error.category) << '/' << error.stage << ": " << error.message;
    if (!error.file.empty()) output << " (file: " << error.file << ")";
    if (!error.detail.empty()) output << " [" << error.detail << "]";
    return output.str();
}

bool parse_error_format(std::string_view value, ErrorFormat &format) {
    if (value == "text") {
        format = ErrorFormat::text;
        return true;
    }
    if (value == "json") {
        format = ErrorFormat::json;
        return true;
    }
    return false;
}

const char *error_format_name(ErrorFormat format) { return format == ErrorFormat::json ? "json" : "text"; }

}   // namespace amc
