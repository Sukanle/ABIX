#ifndef AMC_ERROR_H
#define AMC_ERROR_H

#include <cstdint>
#include <string>
#include <string_view>

namespace amc {

// Unified AMC error schema.
//
// Every AMC tool (amc, amc-dump, ...) reports failures through this one
// object so that machine consumers (AI agents, MCP servers, CI) can parse a
// stable envelope instead of scraping human prose.  `ErrorCategory` is the
// coarse class, `stage` names the operation that failed, and `message`
// carries the human-readable detail.
enum class ErrorCategory : uint32_t {
    usage,
    config,
    io,
    provider,
    format,
    validation,
    compatibility,
    internal,
};

const char *error_category_name(ErrorCategory category);

// Stable, machine-readable error code derived from the category, e.g.
// "AMC-IO" / "AMC-FORMAT".  Kept deliberately coarse; `stage` narrows it.
std::string error_code(ErrorCategory category);

struct Error {
    ErrorCategory category = ErrorCategory::internal;
    std::string stage;
    std::string message;
    std::string file;     // optional offending artifact / path
    std::string detail;   // optional extra context (free-form)
};

Error make_error(
    ErrorCategory category, std::string stage, std::string message, std::string file = {}, std::string detail = {});

// Serialized error envelope, stable across releases:
//   {"schema":"abix.error/1","error":{"code":...,"category":...,
//    "stage":...,"message":...,"file":...,"detail":...}}
std::string error_to_json(const Error &error);

// Human-readable one-liner used when structured output is not requested:
//   amc: <category>/<stage>: <message> (file: <file>) [<detail>]
std::string error_to_text(const Error &error);

enum class ErrorFormat {
    text,
    json
};

bool parse_error_format(std::string_view value, ErrorFormat &format);
const char *error_format_name(ErrorFormat format);

// Escapes a UTF-8 string for embedding in a JSON string literal (no quotes).
std::string json_escape(std::string_view value);

}   // namespace amc

#endif
