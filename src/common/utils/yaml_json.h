#ifndef AGENTICDSL_COMMON_UTILS_YAML_JSON_H
#define AGENTICDSL_COMMON_UTILS_YAML_JSON_H

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

namespace agenticdsl {

// 将 YAML::Node 转换为 nlohmann::json
nlohmann::json yaml_to_json(const YAML::Node& node);

} // namespace agenticdsl

#endif // AGENTICDSL_COMMON_UTILS_YAML_JSON_H
