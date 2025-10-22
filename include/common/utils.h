#ifndef AGENTICDSL_COMMON_UTILS_H
#define AGENTICDSL_COMMON_UTILS_H

#include "types.h"
#include <string>
#include <vector>
#include <regex>
#include <nlohmann/json.hpp>

namespace agenticdsl {

inline std::vector<std::pair<NodePath, std::string>> extract_pathed_blocks(const std::string& markdown_content) {
    std::vector<std::pair<NodePath, std::string>> blocks;

    // Use [\s\S] instead of . to match any character including newlines
    std::regex block_pattern(
        R"(###\s+AgenticDSL\s+`(/[\w/\-]+)`\s*\n"
        R"(# --- BEGIN AgenticDSL ---\s*\n([\s\S]*?)\n"
        R"(# --- END AgenticDSL ---))"
    );

    std::sregex_iterator iter(markdown_content.begin(), markdown_content.end(), block_pattern);
    std::sregex_iterator end;

    for (; iter != end; ++iter) {
        std::string path = (*iter)[1].str();
        std::string yaml_content = (*iter)[2].str();
        blocks.emplace_back(std::move(path), std::move(yaml_content));
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

} // namespace agenticdsl

#endif // AGENTICDSL_COMMON_UTILS_H
