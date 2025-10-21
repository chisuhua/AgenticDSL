#ifndef AGENFLOW_NODES_H
#define AGENFLOW_NODES_H

#include "common/types.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <optional>
#include <vector>

namespace agenticdsl {

// 前向声明渲染器
class InjaTemplateRenderer;

// 节点基类
struct Node {
    NodePath path; // v1.1: Use path instead of id
    NodeType type;
    std::vector<NodePath> next; // v1.1: Allow multiple next paths
    nlohmann::json metadata; // Optional metadata

    virtual ~Node() = default;
    virtual Context execute(Context& context) = 0;
};

// Start节点
struct StartNode : public Node {
    StartNode(NodePath path, std::vector<NodePath> next_paths = {});
    Context execute(Context& context) override;
};

// End节点
struct EndNode : public Node {
    EndNode(NodePath path);
    Context execute(Context& context) override;
};

// Assign节点 - 使用 inja 渲染 (v1.1: renamed from Set)
struct AssignNode : public Node {
    std::unordered_map<std::string, std::string> assign; // key -> Inja template_string

    AssignNode(NodePath path,
               std::unordered_map<std::string, std::string> assigns,
               std::vector<NodePath> next_paths = {});
    Context execute(Context& context) override;
};

// LLM调用节点
struct LLMCallNode : public Node {
    std::string prompt_template; // Inja template
    std::vector<std::string> output_keys; // v1.1: Allow multiple output keys

    LLMCallNode(NodePath path,
                std::string prompt,
                std::vector<std::string> output_keys,
                std::vector<NodePath> next_paths = {});
    Context execute(Context& context) override;
};

// 工具调用节点
struct ToolCallNode : public Node {
    std::string tool_name;
    std::unordered_map<std::string, std::string> arguments; // arg_name -> Inja template_string
    std::vector<std::string> output_keys; // v1.1: Allow multiple output keys

    ToolCallNode(NodePath path,
                 std::string tool,
                 std::unordered_map<std::string, std::string> arguments,
                 std::vector<std::string> output_keys,
                 std::vector<NodePath> next_paths = {});
    Context execute(Context& context) override;
};

// Resource节点 (v1.1: new)
struct ResourceNode : public Node {
    ResourceType resource_type;
    std::string uri;
    std::string scope; // "global" or "local"

    ResourceNode(NodePath path,
                 ResourceType type,
                 std::string uri,
                 std::string scope = "global");
    Context execute(Context& context) override; // This node just registers itself
};

} // namespace agenticdsl

#endif
