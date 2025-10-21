#ifndef AGENFLOW_UTILS_H
#define AGENFLOW_UTILS_H

#include "types.h"
#include <string>
#include <sstream>
#include <vector>
#include <regex>
#include <filesystem>

namespace agenticdsl {

// 从字符串中提取 ```yaml ... ``` 代码块
inline std::string extract_yaml_block(const std::string& text) {
    std::regex yaml_pattern(R"(```\s*yaml\s*\n(.*?)\n```)", std::regex_constants::dotall);
    std::smatch match;
    if (std::regex_search(text, match, yaml_pattern)) {
        return match[1].str();
    }
    return "";
}

// 从 Markdown 内容中提取路径化块
inline std::vector<std::pair<NodePath, std::string>> extract_pathed_blocks(const std::string& markdown_content) {
    std::vector<std::pair<NodePath, std::string>> blocks;
    std::regex block_header_pattern(R"(###\s+AgenticDSL\s+`(/[\w/\-]*)`)");
    std::regex yaml_content_pattern(R"(\# --- BEGIN AgenticDSL ---\s*\n(.*?)\n\# --- END AgenticDSL ---)", std::regex_constants::dotall);

    std::istringstream iss(markdown_content);
    std::string line;
    std::string current_path;
    std::string current_content;
    bool in_block = false;

    while (std::getline(iss, line)) {
        std::smatch header_match;
        if (std::regex_match(line, header_match, block_header_pattern)) {
            if (in_block && !current_path.empty()) {
                blocks.push_back({current_path, current_content});
            }
            current_path = header_match[1].str();
            current_content.clear();
            in_block = true;
        } else if (in_block) {
            std::smatch yaml_match;
            if (std::regex_match(line, yaml_match, yaml_content_pattern)) {
                 current_content = yaml_match[1].str();
                 in_block = false;
            } else {
                current_content += line + "\n";
            }
        }
    }

    // Add the last block
    if (!current_path.empty() && !current_content.empty()) {
        blocks.push_back({current_path, current_content});
    }

    return blocks;
}

// 检查 Context 中是否存在嵌套路径 (e.g., "user.profile.name")
inline bool context_has_path(const Context& ctx, const std::string& path) {
    std::istringstream iss(path);
    std::string segment;
    std::getline(iss, segment, '.');
    
    auto current = ctx;
    if (current.contains(segment)) {
        current = current[segment];
    } else {
        return false;
    }

    while (std::getline(iss, segment, '.')) {
        if (current.is_object() && current.contains(segment)) {
            current = current[segment];
        } else {
            return false;
        }
    }
    return true;
}

// 从 Context 中获取嵌套路径的值 (e.g., "user.profile.name")
inline nlohmann::json get_context_value(const Context& ctx, const std::string& path) {
    std::istringstream iss(path);
    std::string segment;
    std::getline(iss, segment, '.');
    
    auto current = ctx;
    if (current.contains(segment)) {
        current = current[segment];
    } else {
        return nlohmann::json(); // Return null if path not found
    }

    while (std::getline(iss, segment, '.')) {
        if (current.is_object() && current.contains(segment)) {
            current = current[segment];
        } else {
            return nlohmann::json(); // Return null if path not found
        }
    }
    return current;
}

// 检查字符串是否为有效的 JSON
inline bool is_valid_json(const std::string& str) {
    try {
        nlohmann::json::parse(str);
        return true;
    } catch (const nlohmann::json::parse_error&) {
        return false;
    }
}

} // namespace agenticdsl

#endif
