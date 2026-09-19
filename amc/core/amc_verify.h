#ifndef AMC_VERIFY_H
#define AMC_VERIFY_H

#include "amc_core.h"

#include <string>
#include <vector>

namespace amc {

// AI-P1: verify that a runtime implementation still matches its frozen ABI
// contract.
//
// `contract`    — the published/frozen `.abix` (what consumers compiled against)
// `implementation` — metadata regenerated from the current sources
//
// Two modules are consistent only when their type sets, layouts, field sets
// and function signatures are identical.  Additions are reported as drift
// too, because they widen the ABI surface and must be an explicit decision.
enum class ChangeKind {
    type_added,
    type_removed,
    layout_changed,
    field_added,
    field_removed,
    field_offset_changed,
    field_type_changed,
    function_added,
    function_removed,
    function_signature_changed,
    function_parameter_changed,
};

const char *change_kind_name(ChangeKind kind);

struct VerifyChange {
    ChangeKind kind = ChangeKind::type_removed;
    std::string type;     // owning type name (may be empty for free functions)
    std::string name;     // field / function name when applicable
    std::string detail;   // human-readable explanation
};

struct VerifyResult {
    std::string package;
    std::string contract_path;
    std::string implementation_path;
    bool consistent = true;
    std::vector<VerifyChange> changes;
};

bool verify_modules(
    const AbiModule &contract, const AbiModule &implementation, VerifyResult &result, std::string &error);

std::string verify_result_to_text(const VerifyResult &result);
std::string verify_result_to_json(const VerifyResult &result);
std::string verify_report_to_text(const std::vector<VerifyResult> &results);
std::string verify_report_to_json(const std::vector<VerifyResult> &results, bool consistent);

}   // namespace amc

#endif
