#ifndef AGENICDSL_CORE_NODES_H
#define AGENICDSL_CORE_NODES_H

#include "common/types.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace agenticdsl {

// Forward declarations
class InjaTemplateRenderer;

// Base Node
struct Node {
    NodePath path;
    NodeType type;
    std::vector<NodePath> next;
    nlohmann::json metadata;

    Node(NodePath path,
         NodeType type,
         std::vector<NodePath> next = {},
         nlohmann::json metadata = nlohmann::json::object())
        : path(std::move(path)),
          type(type),
          next(std::move(next)),
          metadata(std::move(metadata)) {}

    virtual ~Node() = default;
    [[nodiscard]] virtual Context execute(Context& context) = 0;
};

// Start Node
struct StartNode : public Node {
    StartNode(NodePath path, std::vector<NodePath> next_paths = {});
    [[nodiscard]] Context execute(Context& context) override;
};

// End Node
struct EndNode : public Node {
    EndNode(NodePath path);
    [[nodiscard]] Context execute(Context& context) override;
};

// Assign Node (v1.1: renamed from Set)
struct AssignNode : public Node {
    std::unordered_map<std::string, std::string> assign;

    AssignNode(NodePath path,
               std::unordered_map<std::string, std::string> assigns,
               std::vector<NodePath> next_paths = {});
    [[nodiscard]] Context execute(Context& context) override;
};

// LLM Call Node
struct LLMCallNode : public Node {
    std::string prompt_template;
    std::vector<std::string> output_keys;

    LLMCallNode(NodePath path,
                std::string prompt,
                std::vector<std::string> output_keys,
                std::vector<NodePath> next_paths = {});
    [[nodiscard]] Context execute(Context& context) override;
};

// Tool Call Node
struct ToolCallNode : public Node {
    std::string tool_name;
    std::unordered_map<std::string, std::string> arguments;
    std::vector<std::string> output_keys;

    ToolCallNode(NodePath path,
                 std::string tool_name,
                 std::unordered_map<std::string, std::string> arguments,
                 std::vector<std::string> output_keys,
                 std::vector<NodePath> next_paths = {});
    [[nodiscard]] Context execute(Context& context) override;
};

// Resource Node (v1.1)
struct ResourceNode : public Node {
    ResourceType resource_type;
    std::string uri;
    std::string scope;

    ResourceNode(NodePath path,
                 ResourceType type,
                 std::string uri,
                 std::string scope = "global",
                 nlohmann::json metadata = nlohmann::json::object());
    [[nodiscard]] Context execute(Context& context) override;
};

} // namespace agenticdsl

#endif // AGENICDSL_CORE_NODES_H
