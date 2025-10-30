// modules/parser/include/parser/markdown_parser.h
#ifndef AGENTICDSL_MODULES_PARSER_MARKDOWN_PARSER_H
#define AGENTICDSL_MODULES_PARSER_MARKDOWN_PARSER_H

#include "core/types/node.h" // 引入 Node, NodePath, ParsedGraph
#include "core/types/budget.h" // 引入 ExecutionBudget
#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>

namespace agenticdsl {

class MarkdownParser {
public:
    std::vector<ParsedGraph> parse_from_string(const std::string& markdown_content);
    std::vector<ParsedGraph> parse_from_file(const std::string& file_path);

    std::unique_ptr<Node> create_node_from_json(const NodePath& path, const nlohmann::json& node_json);

private:
    void validate_nodes(const std::vector<std::unique_ptr<Node>>& nodes);
    // Helper to parse signature.outputs into JSON Schema
    std::optional<nlohmann::json> parse_output_schema_from_signature(const std::string& signature_str);
};

} // namespace agenticdsl

#endif // AGENTICDSL_MODULES_PARSER_MARKDOWN_PARSER_H
