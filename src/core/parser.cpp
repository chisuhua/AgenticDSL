#include "agenticdsl/core/parser.h"
#include "agenticdsl/core/nodes.h"
#include "common/utils.h" // v1.1: corrected include
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace agenticdsl {

std::vector<ParsedGraph> MarkdownParser::parse_from_string(const std::string& markdown_content) {
    std::vector<ParsedGraph> graphs;
    auto pathed_blocks = extract_pathed_blocks(markdown_content);

    for (auto& [path, yaml_content] : pathed_blocks) {
        if (!is_valid_node_path(path)) {
            throw std::runtime_error("Invalid node path format: " + path);
        }

        try {
            auto json_doc = nlohmann::json::parse(yaml_content);

            // Handle file-level metadata (e.g., /__meta__)
            if (path == "/__meta__") {
                // Optional: store in global metadata
                continue;
            }

            // Handle subgraph (e.g., /main)
            if (json_doc.contains("graph_type") && json_doc["graph_type"] == "subgraph") {
                ParsedGraph graph;
                graph.path = path;
                graph.metadata = json_doc.value("metadata", nlohmann::json::object());

                if (json_doc.contains("nodes") && json_doc["nodes"].is_array()) {
                    for (const auto& node_json : json_doc["nodes"]) {
                        std::string id = node_json.value("id", "");
                        if (id.empty()) {
                            throw std::runtime_error("Node in subgraph '" + path + "' missing 'id'");
                        }
                        NodePath node_path = path + "/" + id;
                        auto node = create_node_from_json(node_path, node_json);
                        if (node) {
                            graph.nodes.push_back(std::move(node));
                        }
                    }
                }
                graphs.push_back(std::move(graph));
                continue;
            }

            // Handle single node
            if (json_doc.contains("type")) {
                auto node = create_node_from_json(path, json_doc);
                if (node) {
                    ParsedGraph graph;
                    graph.path = path;
                    graph.metadata = json_doc.value("metadata", nlohmann::json::object());
                    graph.nodes.push_back(std::move(node));
                    graphs.push_back(std::move(graph));
                }
            }

        } catch (const nlohmann::json::parse_error& e) {
            throw std::runtime_error("JSON parse error in block '" + path + "': " + std::string(e.what()));
        } catch (const std::exception& e) {
            throw std::runtime_error("Error parsing block '" + path + "': " + std::string(e.what()));
        }
    }

    return graphs;
}

std::vector<ParsedGraph> MarkdownParser::parse_from_file(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + file_path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return parse_from_string(buffer.str());
}

std::unique_ptr<Node> MarkdownParser::create_node_from_json(const NodePath& path, const nlohmann::json& node_json) {
    std::string type_str = node_json.at("type").get<std::string>();

    // Parse next
    std::vector<NodePath> next_paths;
    if (node_json.contains("next")) {
        const auto& next = node_json["next"];
        if (next.is_string()) {
            next_paths.push_back(next.get<std::string>());
        } else if (next.is_array()) {
            for (const auto& np : next) {
                next_paths.push_back(np.get<std::string>());
            }
        }
    }

    // Parse metadata
    nlohmann::json metadata = node_json.value("metadata", nlohmann::json::object());

    if (type_str == "start") {
        return std::make_unique<StartNode>(path, std::move(next_paths));
    } else if (type_str == "end") {
        auto node = std::make_unique<EndNode>(path);
        node->metadata = metadata;
        return node;
    } else if (type_str == "assign") {
        std::unordered_map<std::string, std::string> assign;
        if (node_json.contains("assign") && node_json["assign"].is_object()) {
            for (auto& [key, value] : node_json["assign"].items()) {
                assign[key] = value.get<std::string>();
            }
        }
        return std::make_unique<AssignNode>(path, std::move(assign), std::move(next_paths));
    } else if (type_str == "llm_call") {
        std::string prompt = node_json.at("prompt_template").get<std::string>();
        auto output_keys = parse_output_keys(node_json, path);
        return std::make_unique<LLMCallNode>(path, std::move(prompt), std::move(output_keys), std::move(next_paths));
    } else if (type_str == "tool_call") {
        std::string tool = node_json.at("tool").get<std::string>();
        auto output_keys = parse_output_keys(node_json, path);
        std::unordered_map<std::string, std::string> args;
        if (node_json.contains("arguments") && node_json["arguments"].is_object()) {
            for (auto& [key, value] : node_json["arguments"].items()) {
                args[key] = value.get<std::string>();
            }
        }
        return std::make_unique<ToolCallNode>(path, std::move(tool), std::move(args), std::move(output_keys), std::move(next_paths));
    } else if (type_str == "resource") {
        std::string type_str_lower = node_json.at("resource_type").get<std::string>();
        ResourceType rtype = parse_resource_type(type_str_lower);
        std::string uri = node_json.at("uri").get<std::string>();
        std::string scope = node_json.value("scope", std::string("global"));
        return std::make_unique<ResourceNode>(path, rtype, std::move(uri), std::move(scope), metadata);
    }

    // Unknown type: ignore (forward-compatible per spec)
    return nullptr;
}

void MarkdownParser::validate_nodes(const std::vector<std::unique_ptr<Node>>& nodes) {
    // Optional: implement validation (e.g., path uniqueness, next existence)
    // For v1, rely on executor-level validation
}

} // namespace agenticdsl
