#include "agenticdsl/core/parser.h"
#include "agenticdsl/core/nodes.h"
#include "agenticdsl/dsl/spec.h"
#include "common/utils.h" // v1.1: New include
#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace agenticdsl {

std::vector<ParsedGraph> MarkdownParser::parse_from_string(const std::string& markdown_content) {
    std::vector<ParsedGraph> graphs;

    auto pathed_blocks = extract_pathed_blocks(markdown_content); // v1.1: Use new utility
    for (auto& [path, yaml_content] : pathed_blocks) {
        try {
            nlohmann::json json_doc = nlohmann::json::parse(yaml_content);

            // Check if it's a subgraph definition (v1.1)
            if (json_doc.contains("graph_type") && json_doc["graph_type"] == "subgraph") {
                 ParsedGraph graph;
                 graph.path = path;
                 if (json_doc.contains("metadata")) {
                     graph.metadata = json_doc["metadata"];
                 }
                 // Subgraph nodes are handled differently or ignored in v1
                 // For simplicity, we might parse the 'entry' node and its direct successors here if needed
                 // For now, let's focus on top-level nodes
                 continue; // Skip subgraph definitions for now
            }

            // Assume it's a node definition
            if (json_doc.contains("type")) {
                auto node = create_node_from_json(path, json_doc);
                if (node) {
                    ParsedGraph graph;
                    graph.nodes.push_back(std::move(node));
                    graph.path = path;
                    if (json_doc.contains("metadata")) {
                        graph.metadata = json_doc["metadata"];
                    }
                    graphs.push_back(std::move(graph));
                }
            }
        } catch (const nlohmann::json::parse_error& e) {
            // If parsing fails, skip this block
            continue;
        } catch (const std::exception& e) {
            // Log or handle other errors during node creation
            continue;
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
    std::string content = buffer.str();

    return parse_from_string(content);
}

std::unique_ptr<Node> MarkdownParser::create_node_from_json(const NodePath& path, const nlohmann::json& node_json) {
    std::string type_str = node_json["type"];

    std::vector<NodePath> next_paths;
    if (node_json.contains("next")) {
        if (node_json["next"].is_string()) {
            next_paths.push_back(node_json["next"]);
        } else if (node_json["next"].is_array()) {
            for (auto& next_path : node_json["next"]) {
                next_paths.push_back(next_path.get<std::string>());
            }
        }
    }

    nlohmann::json metadata = nlohmann::json::object();
    if (node_json.contains("metadata") && node_json["metadata"].is_object()) {
        metadata = node_json["metadata"];
    }

    if (type_str == "start") {
        auto node = std::make_unique<StartNode>(std::move(path), std::move(next_paths));
        node->metadata = metadata;
        return std::move(node);
    } else if (type_str == "end") {
        auto node = std::make_unique<EndNode>(std::move(path));
        node->metadata = metadata;
        return std::move(node);
    } else if (type_str == "assign") { // v1.1: renamed from set
        std::unordered_map<std::string, std::string> assign;
        if (node_json.contains("assign") && node_json["assign"].is_object()) {
            for (auto& [key, value] : node_json["assign"].items()) {
                assign[key] = value.get<std::string>();
            }
        }
        auto node = std::make_unique<AssignNode>(std::move(path), std::move(assign), std::move(next_paths));
        node->metadata = metadata;
        return std::move(node);
    } else if (type_str == "llm_call") {
        std::string prompt = node_json["prompt_template"];
        std::vector<std::string> output_keys;
        if (node_json["output_keys"].is_string()) {
            output_keys.push_back(node_json["output_keys"]);
        } else if (node_json["output_keys"].is_array()) {
            for (auto& key : node_json["output_keys"]) {
                output_keys.push_back(key.get<std::string>());
            }
        } else {
            // Handle case where output_keys is missing or not a string/array
            // For v1 compatibility, assume a default key or throw error
            throw std::runtime_error("Invalid or missing output_keys for llm_call node: " + path);
        }
        auto node = std::make_unique<LLMCallNode>(std::move(path), std::move(prompt), std::move(output_keys), std::move(next_paths));
        node->metadata = metadata;
        return std::move(node);
    } else if (type_str == "tool_call") {
        std::string tool = node_json["tool"];
        std::string output_key_str = node_json["output_keys"]; // v1.1: renamed from output_key
        std::vector<std::string> output_keys;
        if (node_json["output_keys"].is_string()) {
            output_keys.push_back(node_json["output_keys"]);
        } else if (node_json["output_keys"].is_array()) {
            for (auto& key : node_json["output_keys"]) {
                output_keys.push_back(key.get<std::string>());
            }
        } else {
            throw std::runtime_error("Invalid or missing output_keys for tool_call node: " + path);
        }
        std::unordered_map<std::string, std::string> args;
        if (node_json.contains("arguments") && node_json["arguments"].is_object()) { // v1.1: renamed from args
            for (auto& [key, value] : node_json["arguments"].items()) {
                args[key] = value.get<std::string>();
            }
        }
        auto node = std::make_unique<ToolCallNode>(std::move(path), std::move(tool), std::move(args), std::move(output_keys), std::move(next_paths));
        node->metadata = metadata;
        return std::move(node);
    } else if (type_str == "resource") { // v1.1: new node type
        ResourceType resource_type;
        std::string type_str_lower = node_json["resource_type"];
        if (type_str_lower == "file") resource_type = ResourceType::FILE;
        else if (type_str_lower == "postgres") resource_type = ResourceType::POSTGRES;
        else if (type_str_lower == "mysql") resource_type = ResourceType::MYSQL;
        else if (type_str_lower == "sqlite") resource_type = ResourceType::SQLITE;
        else if (type_str_lower == "api_endpoint") resource_type = ResourceType::API_ENDPOINT;
        else if (type_str_lower == "vector_store") resource_type = ResourceType::VECTOR_STORE;
        else resource_type = ResourceType::CUSTOM; // Fallback

        std::string uri = node_json["uri"];
        std::string scope = node_json.value("scope", "global"); // Default to global

        auto node = std::make_unique<ResourceNode>(std::move(path), resource_type, std::move(uri), std::move(scope));
        node->metadata = metadata;
        return std::move(node);
    }

    return nullptr; // Unknown type
}

void MarkdownParser::validate_nodes(const std::vector<std::unique_ptr<Node>>& nodes) {
    // Implement validation logic if needed
    // For now, rely on DSLValidator
}

} // namespace agenticdsl
