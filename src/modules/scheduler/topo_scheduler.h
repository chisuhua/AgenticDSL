// modules/scheduler/include/scheduler/topo_scheduler.h
#ifndef AGENTICDSL_MODULES_SCHEDULER_TOPO_SCHEDULER_H
#define AGENTICDSL_MODULES_SCHEDULER_TOPO_SCHEDULER_H

#include "core/types/context.h" // 引入 Context
#include "core/types/node.h"    // 引入 NodePath, Node
#include "core/types/budget.h"  // 引入 ExecutionBudget
#include "scheduler/execution_session.h" // 引入 ExecutionSession
#include "common/tools/registry.h" // 引入 ToolRegistry
#include "common/llm/llama_adapter.h" // 引入 LlamaAdapter
#include "modules/parser/markdown_parser.h" // 引入 ParsedGraph
#include "modules/scheduler/resource_manager.h" // 引入 ParsedGraph
#include <vector>
#include <memory> // For unique_ptr<Node>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <optional>

namespace agenticdsl {

class TopoScheduler {
public:
    struct Config {
        std::optional<ExecutionBudget> initial_budget;
        // Add other config options if needed
        Config() = default;
    };

    TopoScheduler(Config config, ToolRegistry& tool_registry, LlamaAdapter* llm_adapter, const std::vector<ParsedGraph>* full_graphs_ = nullptr);

    void register_node(std::unique_ptr<Node> node);
    void build_dag(); // 构建依赖图
    ExecutionResult execute(Context initial_context);

    // Method for DSLEngine to call to add new graphs dynamically
    void append_dynamic_graphs(std::vector<ParsedGraph> new_graphs);

    std::vector<TraceRecord> get_last_traces() const {
        return session_.get_trace_exporter().get_traces();
    }

private:
    const std::vector<ParsedGraph>* full_graphs_ = nullptr; // ← 新增
    ResourceManager resource_manager_; // ← 成员变量，非全局单例
    ExecutionSession session_;
    std::vector<std::unique_ptr<Node>> all_nodes_;
    std::unordered_map<NodePath, Node*> node_map_;
    std::unordered_map<NodePath, std::vector<NodePath>> reverse_edges_; // 后继 -> 前驱
    std::unordered_map<NodePath, int> in_degree_;
    std::queue<NodePath> ready_queue_;
    std::unordered_set<NodePath> executed_;
    std::vector<NodePath> call_stack_; // 用于 soft end
    //

    void register_resources();

    std::vector<ParsedGraph> dynamic_graphs_; // Store newly generated graphs
    //
    void load_graphs(const std::vector<std::unique_ptr<Node>>& nodes); // Helper for registration/building
    //
    std::optional<NodePath> current_fork_node_path_; // Path of the ForkNode currently being processed
    std::vector<NodePath> current_fork_branches_; // List of branches from the ForkNode
    std::vector<Context> current_fork_branch_results_; // Results from each executed branch
    size_t current_fork_branch_index_ = 0; // Index of the branch currently being executed
    bool is_executing_fork_branches_ = false; // Flag indicating if in branch execution mode
    std::string join_merge_strategy_ = "error_on_conflict"; // Strategy for the corresponding JoinNode
    std::vector<NodePath> join_wait_for_; // Dependencies for the JoinNode (if needed for complex scenarios, but basic impl uses all fork branches)
    std::optional<NodePath> current_join_node_path_; // Path of the JoinNode currently being processed
    // --- END v3.1 ---

    // --- v3.1: Helper methods for Fork/Join ---
    void start_fork_simulation(const ForkNode* fork_node, const Context& fork_context_snapshot);
    void execute_fork_branches();
    Context execute_single_branch(const NodePath& branch_path, const Context& initial_context);
    void finish_fork_simulation();
    void start_join_simulation(const JoinNode* join_node);
    void finish_join_simulation(Context& main_context);
};

} // namespace agenticdsl

#endif // AGENTICDSL_MODULES_SCHEDULER_TOPO_SCHEDULER_H
