#ifndef AMC_DUMP_H
#define AMC_DUMP_H

#include <string>

#include "AMC/Core/Core.h"

namespace amc::dump {

void write_json(const amc::AbiModule &module, std::string &output);
void write_text(const amc::AbiModule &module, std::string &output);
void write_diff_text(
    const amc::AbiModule &source, const amc::AbiModule &target, const amc::AbiModule &report, std::string &output);
void write_diff_json(
    const amc::AbiModule &source, const amc::AbiModule &target, const amc::AbiModule &report, std::string &output);

}   // namespace amc::dump

#endif   // AMC_DUMP_H
