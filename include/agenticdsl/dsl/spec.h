#ifndef AGENFLOW_SPEC_H
#define AGENFLOW_SPEC_H

#include "common/types.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace agenticdsl {

class DSLValidator {
public:
    static bool validate_nodes(const std::vector<nlohmann::json>& nodes_json);
    static bool validate_node_type(const nlohmann::json& node_json);
    static bool validate_graph_structure(const std::vector<nlohmann::json>& nodes_json);

    // v1.1: New validators
    static bool validate_resource_node(const nlohmann::json& node_json);
    static bool validate_path(const std::string& path);

private:
    static bool validate_start_node(const nlohmann::json& node_json);
    static bool validate_end_node(const nlohmann::json& node_json);
    static bool validate_assign_node(const nlohmann::json& node_json); // v1.1: renamed from set
    static bool validate_llm_call_node(const nlohmann::json& node_json);
    static bool validate_tool_call_node(const nlohmann::json& node_json);
};

} // namespace agenticdsl

#endif
