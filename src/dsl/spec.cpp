#include "agenticdsl/dsl/spec.h"
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace agenticdsl {

void NodeValidator::validate(const nlohmann::json& node_json) {
    if (!node_json.is_object()) {
        throw std::runtime_error("Node must be a JSON object");
    }

    if (!node_json.contains("type") || !node_json["type"].is_string()) {
        throw std::runtime_error("Missing or invalid 'type' field");
    }

    std::string type = node_json["type"];
    validate_type(type);

    if (type == "start") validate_start(node_json);
    else if (type == "end") validate_end(node_json);
    else if (type == "assign") validate_assign(node_json);
    else if (type == "llm_call") validate_llm_call(node_json);
    else if (type == "tool_call") validate_tool_call(node_json);
    else if (type == "resource") validate_resource(node_json);
    // Unknown types are allowed (forward compatibility)
}

void NodeValidator::validate_type(const std::string& type) {
    static const std::unordered_set<std::string> valid_types = {
        "start", "end", "assign", "llm_call", "tool_call", "resource"
        // codelet etc. allowed but not validated in v1
    };
    if (valid_types.count(type) == 0) {
        // Allow unknown types for forward compatibility (per spec §4)
        // Do nothing, or log warning if needed
    }
}

void NodeValidator::validate_start(const nlohmann::json& node) {
    if (!node.contains("next")) {
        throw std::runtime_error("start node missing 'next'");
    }
    validate_next(node);
}

void NodeValidator::validate_end(const nlohmann::json& node) {
    // end node has no required fields beyond 'type'
    if (node.contains("output_keys")) {
        validate_output_keys(node);
    }
}

void NodeValidator::validate_assign(const nlohmann::json& node) {
    if (!node.contains("assign") || !node["assign"].is_object()) {
        throw std::runtime_error("assign node requires 'assign' object");
    }
    if (!node.contains("next")) {
        throw std::runtime_error("assign node missing 'next'");
    }
    validate_next(node);
}

void NodeValidator::validate_llm_call(const nlohmann::json& node) {
    if (!node.contains("prompt_template") || !node["prompt_template"].is_string()) {
        throw std::runtime_error("llm_call node requires 'prompt_template' string");
    }
    if (!node.contains("output_keys")) {
        throw std::runtime_error("llm_call node requires 'output_keys'");
    }
    validate_output_keys(node);
    if (!node.contains("next")) {
        throw std::runtime_error("llm_call node missing 'next'");
    }
    validate_next(node);
}

void NodeValidator::validate_tool_call(const nlohmann::json& node) {
    if (!node.contains("tool") || !node["tool"].is_string()) {
        throw std::runtime_error("tool_call node requires 'tool' string");
    }
    if (!node.contains("arguments") || !node["arguments"].is_object()) {
        throw std::runtime_error("tool_call node requires 'arguments' object");
    }
    if (!node.contains("output_keys")) {
        throw std::runtime_error("tool_call node requires 'output_keys'");
    }
    validate_output_keys(node);
    if (!node.contains("next")) {
        throw std::runtime_error("tool_call node missing 'next'");
    }
    validate_next(node);
}

void NodeValidator::validate_resource(const nlohmann::json& node) {
    if (!node.contains("resource_type") || !node["resource_type"].is_string()) {
        throw std::runtime_error("resource node requires 'resource_type' string");
    }
    if (!node.contains("uri") || !node["uri"].is_string()) {
        throw std::runtime_error("resource node requires 'uri' string");
    }
    // 'scope' is optional
}

void NodeValidator::validate_output_keys(const nlohmann::json& node) {
    const auto& ok = node["output_keys"];
    if (!ok.is_string() && !ok.is_array()) {
        throw std::runtime_error("'output_keys' must be string or array");
    }
    if (ok.is_array()) {
        for (const auto& item : ok) {
            if (!item.is_string()) {
                throw std::runtime_error("All items in 'output_keys' array must be strings");
            }
        }
    }
}

void NodeValidator::validate_next(const nlohmann::json& node) {
    const auto& next = node["next"];
    if (!next.is_string() && !next.is_array()) {
        throw std::runtime_error("'next' must be string or array of strings");
    }
    if (next.is_array()) {
        for (const auto& item : next) {
            if (!item.is_string()) {
                throw std::runtime_error("All items in 'next' array must be strings");
            }
        }
    }
}

} // namespace agenticdsl
