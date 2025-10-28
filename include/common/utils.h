#ifndef AGENTICDSL_COMMON_UTILS_H
#define AGENTICDSL_COMMON_UTILS_H

#include "types.h"
#include <string>
#include <vector>
#include <regex>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>  // ← 新增

namespace agenticdsl {

inline std::vector<std::pair<NodePath, std::string>> extract_pathed_blocks(const std::string& markdown_content) {
    std::vector<std::pair<NodePath, std::string>> blocks;

        //R"(###\s+AgenticDSL\s+`(/[\w/\-]+)`\s*\n)"          // ### AgenticDSL `/path`
    std::regex block_pattern(
        R"(#\s+AgenticDSL\s+`([^`]+)`\s*\n)"  // ← 关键修改：[^`]+ 替代 /[\w/\-]+
        R"(```(?:yaml)?\s*\n)"                               // ``` or ```yaml
        R"(# --- BEGIN AgenticDSL ---\s*\n)"                 // BEGIN marker
        R"([\s\S]*?)"                                        // content (non-greedy)
        R"(\n# --- END AgenticDSL ---\s*\n)"                 // END marker
        R"(```)",                                            // closing ```
        std::regex::ECMAScript
    );

    auto begin = std::sregex_iterator(markdown_content.begin(), markdown_content.end(), block_pattern);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        std::string path = (*it)[1].str();        // 第1组：路径
        std::string full_content = (*it)[0].str(); // 整个匹配块
        // 从 full_content 中提取 BEGIN 和 END 之间的 YAML 内容
        size_t begin_pos = full_content.find("# --- BEGIN AgenticDSL ---");
        size_t end_pos = full_content.find("# --- END AgenticDSL ---");
        if (begin_pos != std::string::npos && end_pos != std::string::npos) {
            // 找到 BEGIN 后的第一个换行
            size_t start = full_content.find('\n', begin_pos);
            if (start != std::string::npos) {
                start++; // 跳过换行
                std::string yaml_content = full_content.substr(start, end_pos - start);
                // 去除末尾可能的换行
                if (!yaml_content.empty() && yaml_content.back() == '\n') {
                    yaml_content.pop_back();
                }
                blocks.emplace_back(std::move(path), std::move(yaml_content));
            }
        }
    }
    return blocks;
}

// Validate node path format: must start with / and contain only allowed chars
inline bool is_valid_node_path(const std::string& path) {
    if (path.empty() || path[0] != '/') return false;
    std::regex valid(R"(^/[\w/\-]+$)");
    return std::regex_match(path, valid);
}

// Parse output_keys (string or array) from JSON
inline std::vector<std::string> parse_output_keys(const nlohmann::json& node_json, const NodePath& path) {
    if (!node_json.contains("output_keys")) {
        throw std::runtime_error("Missing 'output_keys' in node: " + path);
    }
    const auto& ok = node_json["output_keys"];
    if (ok.is_string()) {
        return {ok.get<std::string>()};
    } else if (ok.is_array()) {
        std::vector<std::string> keys;
        for (const auto& k : ok) {
            keys.push_back(k.get<std::string>());
        }
        return keys;
    } else {
        throw std::runtime_error("'output_keys' must be string or array in node: " + path);
    }
}

// Parse ResourceType from string
inline ResourceType parse_resource_type(const std::string& type_str) {
    if (type_str == "file") return ResourceType::FILE;
    if (type_str == "postgres") return ResourceType::POSTGRES;
    if (type_str == "mysql") return ResourceType::MYSQL;
    if (type_str == "sqlite") return ResourceType::SQLITE;
    if (type_str == "api_endpoint") return ResourceType::API_ENDPOINT;
    if (type_str == "vector_store") return ResourceType::VECTOR_STORE;
    if (type_str == "custom") return ResourceType::CUSTOM;
    throw std::runtime_error("Unknown resource_type '" + type_str + "'");
}

nlohmann::json yaml_to_json(const YAML::Node& node);

} // namespace agenticdsl

#endif // AGENTICDSL_COMMON_UTILS_H
