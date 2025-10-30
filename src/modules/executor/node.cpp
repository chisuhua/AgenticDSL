// src/core/nodes.cpp
#include "core/types/node.h"
#include <stdexcept>
#include <memory>

namespace agenticdsl {

// ————————————————————————
// StartNode
// ————————————————————————

StartNode::StartNode(NodePath path, std::vector<NodePath> next_paths)
    : Node(std::move(path), NodeType::START, std::move(next_paths)) {}

Context StartNode::execute(Context& context) {
    return context; // 无操作
}

std::unique_ptr<Node> StartNode::clone() const {
    auto node = std::make_unique<StartNode>(path, next);
    node->metadata = metadata;
    node->signature = signature;
    node->permissions = permissions;
    return node;
}

// ————————————————————————
// EndNode
// ————————————————————————

EndNode::EndNode(NodePath path)
    : Node(std::move(path), NodeType::END) {}

Context EndNode::execute(Context& context) {
    return context; // 无操作
}

std::unique_ptr<Node> EndNode::clone() const {
    auto node = std::make_unique<EndNode>(path);
    node->next = next;
    node->metadata = metadata;
    node->signature = signature;
    node->permissions = permissions;
    return node;
}

// ————————————————————————
// AssignNode
// ————————————————————————

AssignNode::AssignNode(NodePath path,
                       std::unordered_map<std::string, std::string> assigns,
                       std::vector<NodePath> next_paths)
    : Node(std::move(path), NodeType::ASSIGN, std::move(next_paths)),
      assign(std::move(assigns)) {}

Context AssignNode::execute(Context& context) {
    return context; // 实际逻辑在 NodeExecutor
}

std::unique_ptr<Node> AssignNode::clone() const {
    auto node = std::make_unique<AssignNode>(path, assign, next);
    node->metadata = metadata;
    node->signature = signature;
    node->permissions = permissions;
    return node;
}

// ————————————————————————
// LLMCallNode
// ————————————————————————

LLMCallNode::LLMCallNode(NodePath path,
                         std::string prompt,
                         std::vector<std::string> output_keys,
                         std::vector<NodePath> next_paths)
    : Node(std::move(path), NodeType::LLM_CALL, std::move(next_paths)),
      prompt_template(std::move(prompt)),
      output_keys(std::move(output_keys)) {}

Context LLMCallNode::execute(Context& context) {
    return context; // 实际逻辑在 NodeExecutor
}

std::unique_ptr<Node> LLMCallNode::clone() const {
    auto node = std::make_unique<LLMCallNode>(path, prompt_template, output_keys, next);
    node->metadata = metadata;
    node->signature = signature;
    node->permissions = permissions;
    return node;
}

// ————————————————————————
// ToolCallNode
// ————————————————————————

ToolCallNode::ToolCallNode(NodePath path,
                           std::string tool_name,
                           std::unordered_map<std::string, std::string> arguments,
                           std::vector<std::string> output_keys,
                           std::vector<NodePath> next_paths)
    : Node(std::move(path), NodeType::TOOL_CALL, std::move(next_paths)),
      tool_name(std::move(tool_name)),
      arguments(std::move(arguments)),
      output_keys(std::move(output_keys)) {}

Context ToolCallNode::execute(Context& context) {
    return context; // 实际逻辑在 NodeExecutor
}

std::unique_ptr<Node> ToolCallNode::clone() const {
    auto node = std::make_unique<ToolCallNode>(path, tool_name, arguments, output_keys, next);
    node->metadata = metadata;
    node->signature = signature;
    node->permissions = permissions;
    return node;
}

// ————————————————————————
// ResourceNode
// ————————————————————————

ResourceNode::ResourceNode(NodePath path,
                           ResourceType type,
                           std::string uri,
                           std::string scope,
                           nlohmann::json metadata)
    : Node(std::move(path), NodeType::RESOURCE, {}, std::move(metadata)),
      resource_type(type),
      uri(std::move(uri)),
      scope(std::move(scope)) {}

Context ResourceNode::execute(Context& context) {
    return context; // 资源注册由 TopoScheduler::build_dag() 完成
}

std::unique_ptr<Node> ResourceNode::clone() const {
    auto node = std::make_unique<ResourceNode>(path, resource_type, uri, scope, metadata);
    return node;
}

// ————————————————————————
// ForkNode (v3.1)
// ————————————————————————

ForkNode::ForkNode(NodePath path, std::vector<NodePath> branch_paths, std::vector<NodePath> next_paths)
    : Node(std::move(path), NodeType::FORK, std::move(next_paths)),
      branches(std::move(branch_paths)) {}

Context ForkNode::execute(Context& context) {
    return context; // 实际逻辑在 TopoScheduler
}

std::unique_ptr<Node> ForkNode::clone() const {
    auto node = std::make_unique<ForkNode>(path, branches, next);
    node->metadata = metadata;
    node->signature = signature;
    node->permissions = permissions;
    return node;
}

// ————————————————————————
// JoinNode (v3.1)
// ————————————————————————

JoinNode::JoinNode(NodePath path, std::vector<NodePath> deps, std::string strategy, std::vector<NodePath> next_paths)
    : Node(std::move(path), NodeType::JOIN, std::move(next_paths)),
      wait_for(std::move(deps)),
      merge_strategy(std::move(strategy)) {}

Context JoinNode::execute(Context& context) {
    return context; // 实际逻辑 in TopoScheduler
}

std::unique_ptr<Node> JoinNode::clone() const {
    auto node = std::make_unique<JoinNode>(path, wait_for, merge_strategy, next);
    node->metadata = metadata;
    node->signature = signature;
    node->permissions = permissions;
    return node;
}

// ————————————————————————
// GenerateSubgraphNode (v3.1)
// ————————————————————————

GenerateSubgraphNode::GenerateSubgraphNode(NodePath path, std::string prompt, std::vector<std::string> output_keys, std::vector<NodePath> next_paths)
    : Node(std::move(path), NodeType::GENERATE_SUBGRAPH, std::move(next_paths)),
      prompt_template(std::move(prompt)),
      output_keys(std::move(output_keys)) {}

Context GenerateSubgraphNode::execute(Context& context) {
    return context; // 实际逻辑 in NodeExecutor
}

std::unique_ptr<Node> GenerateSubgraphNode::clone() const {
    auto node = std::make_unique<GenerateSubgraphNode>(path, prompt_template, output_keys, next);
    node->metadata = metadata;
    node->signature = signature;
    node->permissions = permissions;
    node->signature_validation = signature_validation;
    node->on_signature_violation = on_signature_violation;
    return node;
}

// ————————————————————————
// AssertNode (v3.1)
// ————————————————————————

AssertNode::AssertNode(NodePath path, std::string condition, std::optional<NodePath> on_fail, std::vector<NodePath> next_paths)
    : Node(std::move(path), NodeType::ASSERT, std::move(next_paths)),
      condition(std::move(condition)),
      on_failure(std::move(on_fail)) {}

Context AssertNode::execute(Context& context) {
    return context; // 实际逻辑 in NodeExecutor
}

std::unique_ptr<Node> AssertNode::clone() const {
    auto node = std::make_unique<AssertNode>(path, condition, on_failure, next);
    node->metadata = metadata;
    node->signature = signature;
    node->permissions = permissions;
    return node;
}

} // namespace agenticdsl
