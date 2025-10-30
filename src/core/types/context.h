#ifndef AGENTICDSL_TYPES_CONTEXT_H
#define AGENTICDSL_TYPES_CONTEXT_H

#include <nlohmann/json.hpp>

namespace agenticdsl {

// 使用 nlohmann::json 作为统一的数据类型
using Value = nlohmann::json;
using Context = nlohmann::json;

} // namespace agenticdsl::types

#endif // AGENTICDSL_TYPES_CONTEXT_H
