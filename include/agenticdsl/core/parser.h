#ifndef AGENFLOW_PARSER_H
#define AGENFLOW_PARSER_H

#include "nodes.h"
#include "common/types.h"
#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>

namespace agenticdsl {

struct ParsedGraph {
    std::vector<std::unique_ptr<Node>> nodes;
    NodePath path; // e.g., /main
    nlohmann::json metadata; // graph-level metadata
    std::optional<ExecutionBudget> budget; // ← 新增字段
    //
    std::optional<std::string> signature;
    std::vector<std::string> permissions;
    bool is_standard_library = false; // 路径以 /lib/ 开头
    //
    ParsedGraph() = default;
    ParsedGraph(const ParsedGraph&) = delete;            // ← 禁止拷贝
    ParsedGraph& operator=(const ParsedGraph&) = delete; // ← 禁止拷贝赋值
    ParsedGraph(ParsedGraph&&) = default;                // ← 允许移动
    ParsedGraph& operator=(ParsedGraph&&) = default;     // ← 允许移动赋值
};

class MarkdownParser {
public:
    std::vector<ParsedGraph> parse_from_string(const std::string& markdown_content);
    std::vector<ParsedGraph> parse_from_file(const std::string& file_path);

private:
    std::unique_ptr<Node> create_node_from_json(const NodePath& path, const nlohmann::json& node_json);
    void validate_nodes(const std::vector<std::unique_ptr<Node>>& nodes);
};

} // namespace agenticdsl

#endif
