#include "agenticdsl/dsl/spec.h"
#include <stdexcept>
#include <regex>

namespace agenticdsl {

bool DSLValidator::validate_nodes(const std::vector<nlohmann::json>& nodes_json) {
    if (nodes_json.empty()) {
        throw std::runtime_error("Nodes list cannot be empty");
    }

    // Check for unique paths
    std::unordered_set<std::string> seen_paths;
    for (const auto& node : nodes_json) {
        if (!node.contains("path") || !node["path"].is_string()) {
            throw std::runtime_error("Node missing required 'path' field or 'path' is not a string");
        }
        std::string path = node["path"];
        if (!validate_path(path)) {
            throw std::runtime_error("Invalid node path format: " + path);
        }
        if (seen_paths.count(path)) {
            throw std::runtime_error("Duplicate node path found: " + path);
        }
        seen_paths.insert(path);
    }

    // Validate each node
    for (const auto& node : nodes_json) {
        if (!validate_node_type(node)) {
            return false;
        }
    }

    // Validate graph structure
    if (!validate_graph_structure(nodes_json)) {
        return false;
    }

    return true;
}

bool DSLValidator::validate_path(const std::string& path) {
    // Must start with /
    if (path.empty() || path[0] != '/') {
        return false;
    }
    // Must only contain allowed characters
    std::regex path_regex(R"(^/[\w/\-]*$)");
    return std::regex_match(path, path_regex);
}

bool DSLValidator::validate_node_type(const nlohmann::json& node_json) {
    if (!node_json.contains("type") || !node_json["type"].is_string()) {
        return false;
    }

    std::string type = node_json["type"];
    if (type == "start") return validate_start_node(node_json);
    if (type == "end") return validate_end_node(node_json);
    if (type == "assign") return validate_assign_node(node_json); // v1.1: renamed
    if (type == "llm_call") return validate_llm_call_node(node_json);
    if (type == "tool_call") return validate_tool_call_node(node_json);
    if (type == "resource") return validate_resource_node(node_json); // v1.1: new

    return false; // Unknown type
}

bool DSLValidator::validate_graph_structure(const std::vector<nlohmann::json>& nodes_json) {
    std::unordered_map<std::string, const nlohmann::json*> node_map;
    for (const auto& node : nodes_json) {
        if (node.contains("path")) {
            node_map[node["path"]] = &node;
        }
    }

    // Find start node
    int start_count = 0;
    for (const auto& node : nodes_json) {
        if (node["type"] == "start") {
            start_count++;
        }
    }
    if (start_count != 1) {
        throw std::runtime_error("Must have exactly one start node");
    }

    // Check next references
    for (const auto& node : nodes_json) {
        if (node.contains("next")) {
            if (node["next"].is_string()) {
                std::string next_id = node["next"];
                if (node_map.find(next_id) == node_map.end()) {
                    throw std::runtime_error("Node '" + node["path"].get<std::string>() + "' references non-existent next node: " + next_id);
                }
            } else if (node["next"].is_array()) {
                for (auto& next_id_val : node["next"]) {
                    std::string next_id = next_id_val.get<std::string>();
                    if (node_map.find(next_id) == node_map.end()) {
                        throw std::runtime_error("Node '" + node["path"].get<std::string>() + "' references non-existent next node: " + next_id);
                    }
                }
            }
        }
    }

    // Simple cycle detection for linear flow (v1)
    std::unordered_set<std::string> visited;
    std::string current_id = "";
    for (const auto& node : nodes_json) {
        if (node["type"] == "start") {
            current_id = node["path"];
            break;
        }
    }

    while (!current_id.empty() && !visited.count(current_id)) {
        visited.insert(current_id);
        auto it = node_map.find(current_id);
        if (it == node_map.end()) break;
        const auto& current_node = *(it->second);
        if (current_node.contains("next")) {
            if (current_node["next"].is_string()) {
                current_id = current_node["next"];
            } else if (current_node["next"].is_array() && !current_node["next"].empty()) {
                current_id = current_node["next"][0]; // v1: take first
            } else {
                current_id = "";
            }
        } else {
            current_id = "";
        }
    }

    if (!current_id.empty() && visited.count(current_id)) {
        throw std::runtime_error("Cycle detected in graph");
    }

    return true;
}

bool DSLValidator::validate_start_node(const nlohmann::json& node_json) {
    if (!node_json.contains("path") || !node_json.contains("next")) {
        return false;
    }
    return node_json["path"].is_string() && (node_json["next"].is_string() || node_json["next"].is_array());
}

bool DSLValidator::validate_end_node(const nlohmann::json& node_json) {
    if (!node_json.contains("path")) {
        return false;
    }
    return node_json["path"].is_string();
}

bool DSLValidator::validate_assign_node(const nlohmann::json& node_json) { // v1.1: renamed
    if (!node_json.contains("path") || !node_json.contains("assign") || !node_json.contains("next")) {
        return false;
    }
    return node_json["path"].is_string() &&
           node_json["assign"].is_object() &&
           (node_json["next"].is_string() || node_json["next"].is_array());
}

bool DSLValidator::validate_llm_call_node(const nlohmann::json& node_json) {
    if (!node_json.contains("path") || !node_json.contains("prompt_template") || !node_json.contains("output_keys") || !node_json.contains("next")) {
        return false;
    }
    return node_json["path"].is_string() &&
           node_json["prompt_template"].is_string() &&
           (node_json["output_keys"].is_string() || node_json["output_keys"].is_array()) && // v1.1: allow array
           (node_json["next"].is_string() || node_json["next"].is_array());
}

bool DSLValidator::validate_tool_call_node(const nlohmann::json& node_json) {
    if (!node_json.contains("path") || !node_json.contains("tool") || !node_json.contains("arguments") || !node_json.contains("output_keys") || !node_json.contains("next")) { // v1.1: renamed args to arguments
        return false;
    }
    return node_json["path"].is_string() &&
           node_json["tool"].is_string() &&
           node_json["arguments"].is_object() && // v1.1: renamed
           (node_json["output_keys"].is_string() || node_json["output_keys"].is_array()) && // v1.1: allow array
           (node_json["next"].is_string() || node_json["next"].is_array());
}

bool DSLValidator::validate_resource_node(const nlohmann::json& node_json) { // v1.1: new
    if (!node_json.contains("path") || !node_json.contains("resource_type") || !node_json.contains("uri")) {
        return false;
    }
    return node_json["path"].is_string() &&
           node_json["resource_type"].is_string() &&
           node_json["uri"].is_string();
}

} // namespace agenticdsl
