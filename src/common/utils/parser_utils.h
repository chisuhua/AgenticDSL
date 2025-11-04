#ifndef AGENTICDSL_COMMON_UTILS_PARSER_UTILS_H
#define AGENTICDSL_COMMON_UTILS_PARSER_UTILS_H

#include "core/types/node.h" // 引入 NodePath
#include <string>
#include <vector>
#include <utility> // for std::pair

namespace agenticdsl {

// 从 Markdown 内容中提取带有路径的 DSL 块
std::vector<std::pair<NodePath, std::string>> extract_pathed_blocks(const std::string& markdown_content);

// 验证节点路径格式是否有效
bool is_valid_node_path(const std::string& path);

} // namespace agenticdsl

#endif // AGENTICDSL_COMMON_UTILS_PARSER_UTILS_H
