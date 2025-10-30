// modules/scheduler/include/scheduler/execution_session.h
#ifndef AGENTICDSL_MODULES_SCHEDULER_EXECUTION_SESSION_H
#define AGENTICDSL_MODULES_SCHEDULER_EXECUTION_SESSION_H

#include "core/types/context.h"
#include "core/types/node.h"
#include "core/types/budget.h"
#include "modules/context/context_engine.h"
#include "modules/budget/budget_controller.h"
#include "modules/trace/trace_exporter.h"
#include "modules/executor/node_executor.h"
#include "common/tools/registry.h"
#include "common/llm/llama_adapter.h"
#include "modules/parser/markdown_parser.h" // 引入 MarkdownParser (for GenerateSubgraph)
#include "modules/library/library_loader.h" // ← 新增：用于构建 available_subgraphs
#include "resource_manager.h" // ← 新增：用于构建 available_subgraphs
#include <optional>
#include <vector>
#include <memory>
#include <functional> // For std::function (callback)
#include <unordered_map>
#include <unordered_set>

namespace agenticdsl {

class DSLEngine; // Forward declaration
//
// ExecutionSession 封装了单次执行的所有状态和逻辑
class ExecutionSession {
public:

    using AppendGraphsCallback = std::function<void(std::vector<ParsedGraph>)>;

    ExecutionSession(
        std::optional<ExecutionBudget> initial_budget,
        ToolRegistry& tool_registry,
        LlamaAdapter* llm_adapter,
        ResourceManager& resource_manager, // ← 新增参数
        const std::vector<ParsedGraph>* full_graphs, // ← 新增：指向完整图集
        AppendGraphsCallback append_graphs_callback = nullptr // New parameter
    );

    // 执行一个节点，并处理预算、快照、Trace
    struct ExecutionResult {
        Context new_context;
        bool success;
        std::string message;
        std::optional<NodePath> snapshot_key; // 如果触发了快照
        std::optional<NodePath> paused_at; // 如果暂停在 LLM 调用
    };

    ExecutionResult execute_node(Node* node, const Context& initial_context);
    void check_and_requeue_dynamic_deps(const std::unordered_set<NodePath>& newly_executed_nodes);

    // 检查预算是否超限
    bool is_budget_exceeded() const;

    // 获取 Trace 导出器
    const TraceExporter& get_trace_exporter() const;

    // 获取预算控制器
    const BudgetController& get_budget_controller() const;

    // 获取上下文引擎
    const ContextEngine& get_context_engine() const;
    const std::unordered_map<NodePath, std::vector<NodePath>>& get_pending_dynamic_deps() const { return pending_dynamic_deps_; }
    const std::unordered_map<NodePath, nlohmann::json>& get_dynamic_wait_for_expressions() const { return dynamic_wait_for_expressions_; }

private:
    ResourceManager& resource_manager_; // ← 成员引用
    ContextEngine context_engine_;
    BudgetController budget_controller_;
    TraceExporter trace_exporter_;
    NodeExecutor node_executor_;
    const std::vector<ParsedGraph>* full_graphs_; // ← 指向完整图集
    std::vector<NodePath> call_stack_; // 用于 soft end
    std::unordered_map<NodePath, std::vector<NodePath>> pending_dynamic_deps_; // NodePath -> [list of unresolved deps]
    std::unordered_map<NodePath, nlohmann::json> dynamic_wait_for_expressions_; // NodePath -> original wait_for expression
    AppendGraphsCallback append_graphs_callback_; // Callback for dynamic graphs

    //Context execute_generate_subgraph_with_callback(const GenerateSubgraphNode* node, const Context& ctx);
    nlohmann::json build_available_subgraphs_context() const;
    std::string inject_subgraphs_into_prompt(const std::string& base_prompt, const Context& context) const;

    // Helper to determine if snapshot is needed for a node type
    bool needs_snapshot(Node* node) const;
    std::vector<NodePath> parse_dynamic_wait_for(const nlohmann::json& expr, const Context& ctx);
friend class TopoScheduler; // Grant TopoScheduler access to private members
};

} // namespace agenticdsl

#endif // AGENTICDSL_MODULES_SCHEDULER_EXECUTION_SESSION_H
