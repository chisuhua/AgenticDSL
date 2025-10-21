#ifndef AGENFLOW_PARSER_H
#define AGENFLOW_PARSER_H

#include "nodes.h"
#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>

namespace agenticdsl {

struct ParsedGraph {
    std::vector<std::unique_ptr<Node>> nodes;
    NodePath path; // e.g., /main
    nlohmann::json metadata; // graph-level metadata
};

class MarkdownParser {
public:
    std::vector<ParsedGraph> parse_from_string(const std::string& markdown_content);
    std::vector<ParsedGraph> parse_from_file(const std::string& file_path);

private:
    std::vector<std::pair<NodePath, std::string>> extract_pathed_blocks(const std::string& content);
    std::unique_ptr<Node> create_node_from_json(const NodePath& path, const nlohmann::json& node_json);
    void validate_nodes(const std::vector<std::unique_ptr<Node>>& nodes);
};

} // namespace agenticdsl

#endif
