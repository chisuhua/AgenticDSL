// common/utils/parser_utils.cpp
#include "parser_utils.h"
#include <regex>
#include <string>
#include <vector>
#include <utility> // for std::pair

namespace agenticdsl {

std::vector<std::pair<NodePath, std::string>> extract_pathed_blocks(const std::string& markdown_content) {
    std::vector<std::pair<NodePath, std::string>> blocks;

    // 正则表达式：匹配 AgenticDSL 块
    // 捕获组 1: 路径 (path)
    // 捕获组 2: 完整的 YAML 内容（包含 BEGIN/END 标记）
    std::regex block_pattern(
        R"(#\s+AgenticDSL\s+`([^`]+)`\s*\n)"          // 捕获组 1: 路径
        R"(```(?:yaml)?\s*\n)"                        // ``` 或 ```yaml
        R"(# --- BEGIN AgenticDSL ---\s*\n)"          // BEGIN 标记
        R"([\s\S]*?)"                                 // 捕获内容 (非贪婪)
        R"(\n# --- END AgenticDSL ---\s*\n)"          // END 标记
        R"(```)",                                     // closing ```
        std::regex::ECMAScript
    );

    std::sregex_iterator begin(markdown_content.begin(), markdown_content.end(), block_pattern);
    std::sregex_iterator end;

    for (std::sregex_iterator it = begin; it != end; ++it) {
        std::string path = (*it)[1].str();        // 提取路径
        std::string full_content = (*it)[0].str(); // 提取整个匹配块

        // 从 full_content 中提取 BEGIN 和 END 之间的 YAML 内容
        size_t begin_pos = full_content.find("# --- BEGIN AgenticDSL ---");
        size_t end_pos = full_content.find("# --- END AgenticDSL ---");
        if (begin_pos != std::string::npos && end_pos != std::string::npos) {
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

bool is_valid_node_path(const std::string& path) {
    if (path.empty() || path[0] != '/') return false;
    std::regex valid(R"(^/[\w/\-]+$)");
    return std::regex_match(path, valid);
}

} // namespace agenticdsl
