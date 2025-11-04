// modules/parser/src/markdown_parser.cpp
#include "parser/markdown_parser.h"
#include "common/utils/parser_utils.h"
#include "common/utils/yaml_json.h"
#include "common/utils/template_renderer.h"
#include "core/types/node.h" 
#include "core/types/resource.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>
#include <regex> // For parsing signature (if needed for output_schema)

namespace agenticdsl {

// Helper to parse output_keys (string or array) from JSON
inline std::vector<std::string> parse_output_keys(const nlohmann::json& node_json, const NodePath& path) {
    if (!node_json.contains("output_keys")) {
        throw std::runtime_error("Missing 'output_keys' in node: " + path);
    }
    const auto& ok = node_json["output_keys"];
    if (ok.is_string()) {
        return {ok.get<std::string>()};
    } else if (ok.is_array()) {
        std::vector<std::string> keys;
        for (const auto& k : ok) {
            keys.push_back(k.get<std::string>());
        }
        return keys;
    } else {
        throw std::runtime_error("'output_keys' must be string or array in node: " + path);
    }
}

// Parse ResourceType from string
inline ResourceType parse_resource_type(const std::string& type_str) {
    if (type_str == "file") return ResourceType::FILE;
    if (type_str == "postgres") return ResourceType::POSTGRES;
    if (type_str == "mysql") return ResourceType::MYSQL;
    if (type_str == "sqlite") return ResourceType::SQLITE;
    if (type_str == "api_endpoint") return ResourceType::API_ENDPOINT;
    if (type_str == "vector_store") return ResourceType::VECTOR_STORE;
    if (type_str == "custom") return ResourceType::CUSTOM;
    throw std::runtime_error("Unknown resource_type '" + type_str + "'");
}

std::vector<ParsedGraph> MarkdownParser::parse_from_string(const std::string& markdown_content) {
    std::vector<ParsedGraph> graphs;
    std::optional<ExecutionBudget> global_budget; // 临时存储 /__meta__ 中的预算
    auto pathed_blocks = extract_pathed_blocks(markdown_content);

    for (auto& [path, yaml_content] : pathed_blocks) {
        if (!is_valid_node_path(path)) {
            throw std::runtime_error("Invalid node path format: " + path);
        }

        try {
            YAML::Node yaml_root = YAML::Load(yaml_content);
            nlohmann::json json_doc = yaml_to_json(yaml_root);

            if (path == "/__meta__") {
                // 提取 execution_budget（如果存在）
                if (json_doc.contains("execution_budget")) {
                    const auto& bj = json_doc["execution_budget"];
                    ExecutionBudget budget;
                    if (bj.contains("max_nodes") && bj["max_nodes"].is_number_integer()) {
                        budget.max_nodes = bj["max_nodes"].get<int>();
                    }
                    if (bj.contains("max_llm_calls") && bj["max_llm_calls"].is_number_integer()) {
                        budget.max_llm_calls = bj["max_llm_calls"].get<int>();
                    }
                    if (bj.contains("max_duration_sec") && bj["max_duration_sec"].is_number_integer()) {
                        budget.max_duration_sec = bj["max_duration_sec"].get<int>();
                    }
                    if (bj.contains("max_subgraph_depth") && bj["max_subgraph_depth"].is_number_integer()) {
                        budget.max_subgraph_depth = bj["max_subgraph_depth"].get<int>();
                    }
                    if (bj.contains("max_snapshots") && bj["max_snapshots"].is_number_integer()) {
                        budget.max_snapshots = bj["max_snapshots"].get<int>();
                    }
                    if (bj.contains("snapshot_max_size_kb") && bj["snapshot_max_size_kb"].is_number_integer()) {
                        budget.snapshot_max_size_kb = bj["snapshot_max_size_kb"].get<size_t>();
                    }
                    global_budget = std::move(budget);
                }
                continue; // /__meta__ 不是可执行的子图，跳过
            }

            // Handle subgraph (e.g., /main, /lib/reasoning/example)
            if (json_doc.contains("graph_type") && json_doc["graph_type"] == "subgraph") {
                ParsedGraph graph;
                graph.path = path;
                graph.metadata = json_doc.value("metadata", nlohmann::json::object());

                if (json_doc.contains("signature")) {
                    graph.signature = json_doc["signature"].get<std::string>();
                    // v3.1: Parse output_schema from signature
                    graph.output_schema = parse_output_schema_from_signature(graph.signature.value());
                }
                if (json_doc.contains("permissions") && json_doc["permissions"].is_array()) {
                    for (const auto& p : json_doc["permissions"]) {
                        if (p.is_string()) {
                            graph.permissions.push_back(p.get<std::string>());
                        }
                    }
                }
                graph.is_standard_library = (path.rfind("/lib/", 0) == 0); // 以 /lib/ 开头

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

            // Handle single node (standalone block representing one node)
            if (json_doc.contains("type")) {
                auto node = create_node_from_json(path, json_doc);
                if (node) {
                    ParsedGraph graph;
                    graph.path = path;
                    graph.metadata = json_doc.value("metadata", nlohmann::json::object());

                    // 单节点图也可有 signature
                    if (json_doc.contains("signature")) {
                        graph.signature = json_doc["signature"].get<std::string>();
                        // v3.1: Parse output_schema from signature
                        graph.output_schema = parse_output_schema_from_signature(graph.signature.value());
                    }
                    if (json_doc.contains("permissions") && json_doc["permissions"].is_array()) {
                        for (const auto& p : json_doc["permissions"]) {
                            if (p.is_string()) {
                                graph.permissions.push_back(p.get<std::string>());
                            }
                        }
                    }
                    graph.is_standard_library = (path.rfind("/lib/", 0) == 0);

                    graph.nodes.push_back(std::move(node));
                    graphs.push_back(std::move(graph));
                }
            }
        } catch (const YAML::ParserException& e) {
            throw std::runtime_error("YAML parse error in block '" + path + "': " + e.what());
        } catch (const std::exception& e) {
            throw std::runtime_error("Error parsing block '" + path + "': " + std::string(e.what()));
        }
    }

    // 将 global_budget 注入到 graphs 中（例如附加到第一个图，或单独存储）
    // 方案：注入到第一个图（或任意图），TopoScheduler 会检查
    if (global_budget.has_value() && !graphs.empty()) {
        graphs[0].budget = std::move(global_budget);
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
    std::cout << "[DEBUG] Parsing node at " << path << ": " << node_json.dump(2) << std::endl;
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

    // Extract node-level signature / permissions (v3.1)
    std::optional<std::string> signature = std::nullopt;
    std::vector<std::string> permissions;
    if (node_json.contains("signature")) {
        signature = node_json["signature"].get<std::string>();
    }
    if (node_json.contains("permissions") && node_json["permissions"].is_array()) {
        for (const auto& p : node_json["permissions"]) {
            if (p.is_string()) {
                permissions.push_back(p.get<std::string>());
            }
        }
    }

    if (type_str == "start") {
        auto node = std::make_unique<StartNode>(path, std::move(next_paths));
        node->metadata = metadata;
        node->signature = signature;
        node->permissions = permissions;
        return node;
    } else if (type_str == "end") {
        auto node = std::make_unique<EndNode>(path);
        node->next = std::move(next_paths);
        node->metadata = metadata;
        node->signature = signature;
        node->permissions = permissions;
        return node;
    } else if (type_str == "assign") {
        std::unordered_map<std::string, std::string> assign;
        if (node_json.contains("assign") && node_json["assign"].is_object()) {
            for (auto& [key, value] : node_json["assign"].items()) {
                assign[key] = value.get<std::string>();
            }
        }
        auto node = std::make_unique<AssignNode>(path, std::move(assign), std::move(next_paths));
        node->metadata = metadata;
        node->signature = signature;
        node->permissions = permissions;
        return node;
    } else if (type_str == "llm_call") {
        std::string prompt = node_json.at("prompt_template").get<std::string>();
        auto output_keys = parse_output_keys(node_json, path);
        auto node = std::make_unique<LLMCallNode>(path, std::move(prompt), std::move(output_keys), std::move(next_paths));
        node->metadata = metadata;
        node->signature = signature;
        node->permissions = permissions;
        return node;
    } else if (type_str == "tool_call") {
        std::string tool = node_json.at("tool").get<std::string>();
        auto output_keys = parse_output_keys(node_json, path);
        std::unordered_map<std::string, std::string> args;
        if (node_json.contains("arguments") && node_json["arguments"].is_object()) {
            for (auto& [key, value] : node_json["arguments"].items()) {
                if (!value.is_string()) {
                    throw std::runtime_error("Argument '" + key + "' is not a string");
                }
                args[key] = value.get<std::string>();
            }
        }
        auto node = std::make_unique<ToolCallNode>(path, std::move(tool), std::move(args), std::move(output_keys), std::move(next_paths));
        node->metadata = metadata;
        node->signature = signature;
        node->permissions = permissions;
        return node;
    } else if (type_str == "resource") {
        std::string type_str_lower = node_json.at("resource_type").get<std::string>();
        ResourceType rtype = parse_resource_type(type_str_lower);
        std::string uri = node_json.at("uri").get<std::string>();
        std::string scope = node_json.value("scope", std::string("global"));
        auto node = std::make_unique<ResourceNode>(path, rtype, std::move(uri), std::move(scope), metadata);
        node->signature = signature;
        node->permissions = permissions;
        return node;
    } else if (type_str == "fork") {
        std::vector<NodePath> branches;
        if (node_json.contains("fork") && node_json["fork"].contains("branches")) {
            const auto& branches_json = node_json["fork"]["branches"];
            if (branches_json.is_array()) {
                for (const auto& b : branches_json) {
                    branches.push_back(b.get<std::string>());
                }
            } else if (branches_json.is_string()) {
                 branches.push_back(branches_json.get<std::string>());
            }
        }
        auto node = std::make_unique<ForkNode>(path, std::move(branches), std::move(next_paths));
        node->metadata = metadata;
        node->signature = signature;
        node->permissions = permissions;
        return node;
    } else if (type_str == "join") {
        std::vector<NodePath> deps;
        std::string strategy = "error_on_conflict"; // Default
        if (node_json.contains("join")) {
            const auto& join_obj = node_json["join"];
            if (join_obj.contains("wait_for")) {
                const auto& deps_json = join_obj["wait_for"];
                if (deps_json.is_array()) {
                    for (const auto& d : deps_json) {
                        deps.push_back(d.get<std::string>());
                    }
                } else if (deps_json.is_string()) {
                    deps.push_back(deps_json.get<std::string>());
                }
            }
            if (join_obj.contains("merge_strategy")) {
                strategy = join_obj["merge_strategy"].get<std::string>();
            }
        }
        auto node = std::make_unique<JoinNode>(path, std::move(deps), std::move(strategy), std::move(next_paths));
        node->metadata = metadata;
        node->signature = signature;
        node->permissions = permissions;
        return node;
    } else if (type_str == "generate_subgraph") {
        std::string prompt = node_json.at("prompt_template").get<std::string>();
        auto output_keys = parse_output_keys(node_json, path); // Reuse existing helper
        std::string sig_validation = node_json.value("signature_validation", std::string("strict"));
        std::optional<NodePath> on_violation = std::nullopt;
        if (node_json.contains("on_signature_violation") && node_json["on_signature_violation"].is_string()) {
            on_violation = node_json["on_signature_violation"].get<std::string>();
        }
        auto node = std::make_unique<GenerateSubgraphNode>(path, std::move(prompt), std::move(output_keys), std::move(next_paths));
        node->signature_validation = sig_validation;
        node->on_signature_violation = on_violation;
        node->metadata = metadata;
        node->signature = signature;
        node->permissions = permissions;
        return node;
    } else if (type_str == "assert") {
        std::string condition = node_json.at("condition").get<std::string>();
        std::optional<NodePath> on_failure = std::nullopt;
        if (node_json.contains("on_failure") && node_json["on_failure"].is_string()) {
            on_failure = node_json["on_failure"].get<std::string>();
        }
        auto node = std::make_unique<AssertNode>(path, std::move(condition), std::move(on_failure), std::move(next_paths));
        node->metadata = metadata;
        node->signature = signature;
        node->permissions = permissions;
        return node;
    }


    // Unknown type: ignore (forward-compatible per spec)
    return nullptr;
}

void MarkdownParser::validate_nodes(const std::vector<std::unique_ptr<Node>>& nodes) {
    // Optional: implement validation (e.g., path uniqueness, next existence)
    // For v1, rely on executor-level validation
}

// v3.1: Helper to parse signature.outputs into JSON Schema
std::optional<nlohmann::json> MarkdownParser::parse_output_schema_from_signature(const std::string& signature_str) {
    // This is a simplified example. A full implementation would require a proper parser
    // for the signature format defined in the spec (e.g., "(input: string) -> {result: number}")
    // For now, we'll look for a pattern like " -> { ... }" and assume it's valid JSON schema.
    std::regex output_pattern(R"(\s*->\s*(\{.*\}))");
    std::smatch match;
    if (std::regex_search(signature_str, match, output_pattern)) {
        try {
            std::string schema_str = match[1].str();
            nlohmann::json schema = nlohmann::json::parse(schema_str);
            return schema;
        } catch (const std::exception& e) {
            // If parsing the output part fails, log a warning but don't fail the whole parse
            std::cerr << "[WARNING] Could not parse output schema from signature '" << signature_str << "': " << e.what() << std::endl;
            // Return empty object or null if parsing fails
            return nlohmann::json::object();
        }
    }
    // If no output part is found, return nullopt
    return std::nullopt;
}

} // namespace agenticdsl
