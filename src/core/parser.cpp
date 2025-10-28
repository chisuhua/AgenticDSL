#include "agenticdsl/core/parser.h"
#include "agenticdsl/core/nodes.h"
#include "common/utils.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

namespace agenticdsl {

std::vector<ParsedGraph> MarkdownParser::parse_from_string(const std::string& markdown_content) {
    std::vector<ParsedGraph> graphs;
    std::optional<ExecutionBudget> global_budget; // ← 临时存储
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
                    global_budget = budget;
                }
                continue;
            }

            // Handle subgraph (e.g., /main)
            if (json_doc.contains("graph_type") && json_doc["graph_type"] == "subgraph") {
                ParsedGraph graph;
                graph.path = path;
                graph.metadata = json_doc.value("metadata", nlohmann::json::object());

                if (json_doc.contains("signature")) {
                    graph.signature = json_doc["signature"].get<std::string>();
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

            // Handle single node
            if (json_doc.contains("type")) {
                auto node = create_node_from_json(path, json_doc);
                if (node) {
                    ParsedGraph graph;
                    graph.path = path;
                    graph.metadata = json_doc.value("metadata", nlohmann::json::object());

                    // 单节点图也可有 signature（较少见，但允许）
                    if (json_doc.contains("signature")) {
                        graph.signature = json_doc["signature"].get<std::string>();
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
    // 方案：注入到第一个图（或任意图），DAGExecutor 会遍历查找
    if (global_budget.has_value() && !graphs.empty()) {
        graphs[0].budget = global_budget;
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

    // === 阶段 3.2: 提取节点级 signature / permissions ===
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
                    // 这不应该发生，如果发生了说明 yaml_to_json 有问题
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
    }

    // Unknown type: ignore (forward-compatible per spec)
    return nullptr;
}

void MarkdownParser::validate_nodes(const std::vector<std::unique_ptr<Node>>& nodes) {
    // Optional: implement validation (e.g., path uniqueness, next existence)
    // For v1, rely on executor-level validation
}

} // namespace agenticdsl
