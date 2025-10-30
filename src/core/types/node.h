#ifndef AGENTICDSL_COMMON_TYPES_NODE_H
#define AGENTICDSL_COMMON_TYPES_NODE_H

#include "context.h" // 引入 Context/Value
#include "budget.h" // 引入 ResourceType
#include "resource.h" // 引入 ResourceType
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <optional>

namespace agenticdsl {

// 节点路径类型
using NodePath = std::string; // e.g., "/main/step1"

// 节点类型枚举
enum class NodeType : uint8_t {
    START,
    END,
    ASSIGN,
    LLM_CALL,
    TOOL_CALL,
    RESOURCE,
    FORK,
    JOIN,
    GENERATE_SUBGRAPH,
    ASSERT
};

// Forward declarations for Node structure
class InjaTemplateRenderer; // Declared here, defined elsewhere

// Base Node
struct Node {
    NodePath path;
    NodeType type;
    std::vector<NodePath> next;
    nlohmann::json metadata;

    std::optional<std::string> signature;     // e.g., "(input: string) -> {result: number}"
    std::vector<std::string> permissions;     // e.g., ["network", "file:read"]

    Node(NodePath path,
         NodeType type,
         std::vector<NodePath> next = {},
         nlohmann::json metadata = nlohmann::json::object(),
         std::optional<std::string> sig = std::nullopt,
         std::vector<std::string> perms = {})
        : path(std::move(path)),
          type(type),
          next(std::move(next)),
          metadata(std::move(metadata)),
          signature(std::move(sig)),
          permissions(std::move(perms)) {}

    virtual ~Node() = default;
    [[nodiscard]] virtual Context execute(Context& context) = 0;
    virtual std::unique_ptr<Node> clone() const = 0; // Required for scheduler
};

struct ParsedGraph {
    std::vector<std::unique_ptr<Node>> nodes;
    NodePath path; // e.g., /main
    nlohmann::json metadata; // graph-level metadata
    std::optional<ExecutionBudget> budget; // 从 /__meta__ 解析
    std::optional<std::string> signature; // 子图签名
    std::vector<std::string> permissions; // 子图权限
    bool is_standard_library = false; // 路径以 /lib/ 开头
    std::optional<nlohmann::json> output_schema; // v3.1: 解析 signature.outputs 为 JSON Schema
    
    ParsedGraph() = default;
    ParsedGraph(ParsedGraph&&) = default;                // 允许移动
    ParsedGraph& operator=(ParsedGraph&&) = default;     // 允许移动赋值
    ParsedGraph(const ParsedGraph&) = delete;            // 禁止拷贝
    ParsedGraph& operator=(const ParsedGraph&) = delete; // 禁止拷贝赋值
};


// Start Node
struct StartNode : public Node {
    StartNode(NodePath path, std::vector<NodePath> next_paths = {});
    [[nodiscard]] Context execute(Context& context) override;
    std::unique_ptr<Node> clone() const override;
};

// End Node
struct EndNode : public Node {
    EndNode(NodePath path);
    [[nodiscard]] Context execute(Context& context) override;
    std::unique_ptr<Node> clone() const override;
};

// Assign Node (v1.1: renamed from Set)
struct AssignNode : public Node {
    std::unordered_map<std::string, std::string> assign;

    AssignNode(NodePath path,
               std::unordered_map<std::string, std::string> assigns,
               std::vector<NodePath> next_paths = {});
    [[nodiscard]] Context execute(Context& context) override;
    std::unique_ptr<Node> clone() const override;
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
    std::unique_ptr<Node> clone() const override;
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
    std::unique_ptr<Node> clone() const override;
};

// Resource Node (v1.1)
struct ResourceNode : public Node {
    ResourceType resource_type; // ResourceType must be defined in resource.h
    std::string uri;
    std::string scope;

    ResourceNode(NodePath path,
                 ResourceType type,
                 std::string uri,
                 std::string scope = "global",
                 nlohmann::json metadata = nlohmann::json::object());
    [[nodiscard]] Context execute(Context& context) override;
    std::unique_ptr<Node> clone() const override;
};

// --- 新增：Fork Node (v3.1) ---
struct ForkNode : public Node {
    std::vector<NodePath> branches; // List of subgraph paths to execute in parallel

    ForkNode(NodePath path, std::vector<NodePath> branch_paths, std::vector<NodePath> next_paths = {});
    [[nodiscard]] Context execute(Context& context) override; // Implementation in executor
    std::unique_ptr<Node> clone() const override;
};

// --- 新增：Join Node (v3.1) ---
struct JoinNode : public Node {
    std::vector<NodePath> wait_for; // List of nodes to wait for
    std::string merge_strategy; // e.g., "error_on_conflict", "last_write_wins", etc.

    JoinNode(NodePath path, std::vector<NodePath> deps, std::string strategy, std::vector<NodePath> next_paths = {});
    [[nodiscard]] Context execute(Context& context) override; // Implementation in executor
    std::unique_ptr<Node> clone() const override;
};

// --- 新增：GenerateSubgraph Node (v3.1) ---
struct GenerateSubgraphNode : public Node {
    std::string prompt_template;
    std::vector<std::string> output_keys; // e.g., ["generated_graph_path"]
    std::string signature_validation = "strict"; // v3.1: strict, warn, ignore
    std::optional<NodePath> on_signature_violation; // v3.1

    GenerateSubgraphNode(NodePath path, std::string prompt, std::vector<std::string> output_keys, std::vector<NodePath> next_paths = {});
    [[nodiscard]] Context execute(Context& context) override; // Implementation in executor
    std::unique_ptr<Node> clone() const override;
};

struct AssertNode : public Node {
    std::string condition; // Inja boolean expression
    std::optional<NodePath> on_failure; // Jump path on failure

    AssertNode(NodePath path, std::string condition, std::optional<NodePath> on_fail, std::vector<NodePath> next_paths = {});
    [[nodiscard]] Context execute(Context& context) override;
    std::unique_ptr<Node> clone() const override;
};


} // namespace agenticdsl

#endif // AGENTICDSL_COMMON_TYPES_NODE_H
