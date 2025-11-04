    
---
# 项目文件组织结构

*此目录结构列出了本次导出包含的所有文件*

- budget    
    - include    
        - budget    
            - `budget_controller.h`    
    - src    
        - `budget_controller.cpp`    
- context    
    - include    
        - context    
            - `context_engine.h`    
    - src    
        - `context_engine.cpp`    
- executor    
    - include    
        - executor    
            - `node_executor.h`    
    - src    
        - `node_executor.cpp`    
- library    
    - include    
        - library    
            - `library_loader.h`    
            - `schema.h`    
    - src    
        - `library_loader.cpp`    
- parser    
    - include    
        - parser    
            - `markdown_parser.h`    
    - src    
        - `markdown_parser.cpp`    
- scheduler    
    - include    
        - scheduler    
            - `execution_session.h`    
            - `resource_manager.h`    
            - `topo_scheduler.h`    
    - src    
        - `execution_session.cpp`    
        - `resource_manager.cpp`    
        - `topo_scheduler.cpp`    
- system    
    - include    
        - system    
            - `system_nodes.h`    
    - src    
        - `system_nodes.cpp`    
- trace    
    - include    
        - trace    
            - `trace_exporter.h`    
    - src    
        - `trace_exporter.cpp`    
    
    
## `context/include/context/context_engine.h`
    
```cpp
// modules/context/include/context/context_engine.h
#ifndef AGENTICDSL_MODULES_CONTEXT_CONTEXT_ENGINE_H
#define AGENTICDSL_MODULES_CONTEXT_CONTEXT_ENGINE_H

#include "common/types/context.h" // 引入 Context, Value
#include "common/types/node.h"    // 引入 NodePath
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace agenticdsl {

// 定义合并策略类型
using MergeStrategy = std::string; // "error_on_conflict", "last_write_wins", "deep_merge", "array_concat", "array_merge_unique"
using MergePolicy = std::function<void(agenticdsl::Context&, const agenticdsl::Context&)>;

// 合并策略配置
struct ContextMergePolicy {
    std::unordered_map<std::string, MergeStrategy> field_policies; // 通配符或精确路径
    MergeStrategy default_strategy = "error_on_conflict";
};

class ContextEngine {
public:
    struct Result {
        Context new_context;
        std::optional<NodePath> snapshot_key; // 如果本次执行触发了快照
    };

    // 执行节点并根据需要处理快照（聚合接口）
    Result execute_with_snapshot(
        std::function<Context(const Context&)> execute_func, // 执行节点的函数
        const Context& ctx,
        bool need_snapshot,
        const NodePath& snapshot_node_path // 触发快照的节点路径
    );

    // 静态合并方法（供 executor 内部或其它需要合并的地方使用）
    static void merge(Context& target, const Context& source, const ContextMergePolicy& policy = {});

    // 保存快照
    void save_snapshot(const NodePath& key, const Context& ctx);

    // 获取快照（只读）
    const Context* get_snapshot(const NodePath& key) const;

    // 清理快照（FIFO，根据 max_count 和 max_size）
    void enforce_snapshot_budget();

    // 设置快照预算限制
    void set_snapshot_limits(size_t max_count, size_t max_size_kb);

private:
    std::unordered_map<NodePath, Context> snapshots_;
    std::vector<NodePath> snapshot_order_; // 用于 FIFO
    size_t max_snapshots_ = 10; // 可配置，默认 dev=10, prod=0
    size_t max_snapshot_size_kb_ = 512; // 可配置
    size_t current_total_size_kb_ = 0; // 估算的总大小

    // Helper for merging
    static void merge_recursive(Context& target, const Context& source, const std::string& path_prefix, const ContextMergePolicy& policy);
    static void merge_array(Context& target_arr, const Context& source_arr, MergeStrategy strategy);
    static void merge_scalar(Context& target_val, const Context& source_val, MergeStrategy strategy);
};

} // namespace agenticdsl

#endif // AGENTICDSL_MODULES_CONTEXT_CONTEXT_ENGINE_H

```
    
## `context/src/context_engine.cpp`
    
```cpp
// modules/context/src/context_engine.cpp
#include "context/context_engine.h"
#include <algorithm>
#include <sstream>
#include <iostream> // For debugging if needed

namespace agenticdsl {

// --- Helper functions ---

// Calculate approximate size of a JSON object in KB
inline size_t estimate_json_size_kb(const nlohmann::json& j) {
    std::ostringstream oss;
    oss << j; // This is a very rough estimation
    return (oss.str().size() + 1023) / 1024; // Round up to KB
}

// Get merge strategy for a specific path based on policies
inline MergeStrategy get_merge_strategy_for_path(const std::string& path, const ContextMergePolicy& policy) {
    // Check for exact match first
    auto exact_it = policy.field_policies.find(path);
    if (exact_it != policy.field_policies.end()) {
        return exact_it->second;
    }

    // Check for wildcard matches (e.g., "results.*" matches "results.items[0]")
    for (const auto& [pattern, strategy] : policy.field_policies) {
        if (pattern.back() == '*') {
            std::string prefix = pattern.substr(0, pattern.length() - 1);
            if (path.starts_with(prefix)) {
                return strategy;
            }
        }
    }
    // Fallback to default
    return policy.default_strategy;
}

// --- ContextEngine Implementation ---

ContextEngine::Result ContextEngine::execute_with_snapshot(
    std::function<Context(const Context&)> execute_func,
    const Context& ctx,
    bool need_snapshot,
    const NodePath& snapshot_node_path) {

    Result res;
    res.new_context = execute_func(ctx);

    if (need_snapshot) {
        save_snapshot(snapshot_node_path, ctx); // Snapshot *before* execution
        res.snapshot_key = snapshot_node_path;
    }

    return res;
}

void ContextEngine::merge(Context& target, const Context& source, const ContextMergePolicy& policy) {
    if (!source.is_object()) {
        // Cannot merge non-object into object
        return;
    }
    merge_recursive(target, source, "", policy);
}

void ContextEngine::merge_recursive(Context& target, const Context& source, const std::string& path_prefix, const ContextMergePolicy& policy) {
    if (!target.is_object() || !source.is_object()) {
        // If target or source is not an object, merge as scalars/arrays using default strategy
        if (source.is_array()) {
            merge_array(target, source, policy.default_strategy);
        } else {
            merge_scalar(target, source, policy.default_strategy);
        }
        return;
    }

    for (auto it = source.begin(); it != source.end(); ++it) {
        std::string current_path = path_prefix.empty() ? it.key() : path_prefix + "." + it.key();

        auto target_it = target.find(it.key());
        if (target_it == target.end()) {
            // Field doesn't exist in target, just assign
            target[it.key()] = it.value();
        } else {
            // Field exists in target, apply merge strategy
            MergeStrategy strategy = get_merge_strategy_for_path(current_path, policy);

            if (target_it.value().is_object() && it.value().is_object()) {
                // Recursively merge objects
                merge_recursive(target_it.value(), it.value(), current_path, policy);
            } else if (target_it.value().is_array() && it.value().is_array()) {
                // Merge arrays according to strategy
                merge_array(target_it.value(), it.value(), strategy);
            } else {
                // Merge scalars or mismatched types according to strategy
                merge_scalar(target_it.value(), it.value(), strategy);
            }
        }
    }
}

void ContextEngine::merge_array(Context& target_arr, const Context& source_arr, MergeStrategy strategy) {
    if (strategy == "array_concat") {
        if (!target_arr.is_array()) target_arr = nlohmann::json::array();
        for (const auto& item : source_arr) {
            target_arr.push_back(item);
        }
    } else if (strategy == "array_merge_unique") {
        if (!target_arr.is_array()) target_arr = nlohmann::json::array();
        for (const auto& item : source_arr) {
            if (std::find(target_arr.begin(), target_arr.end(), item) == target_arr.end()) {
                target_arr.push_back(item);
            }
        }
    } else if (strategy == "deep_merge") {
        // For arrays, deep_merge means replacement, not concatenation
        target_arr = source_arr;
    } else if (strategy == "last_write_wins") {
        target_arr = source_arr;
    } else { // error_on_conflict, default
        throw std::runtime_error("Context merge conflict for array field.");
    }
}

void ContextEngine::merge_scalar(Context& target_val, const Context& source_val, MergeStrategy strategy) {
    if (strategy == "last_write_wins") {
        target_val = source_val;
    } else if (strategy == "deep_merge") {
        // For scalars, deep_merge means replacement
        target_val = source_val;
    } else { // error_on_conflict (default), last_write_wins for non-arrays
        if (target_val != source_val) {
            throw std::runtime_error("Context merge conflict for scalar field: " + target_val.dump() + " vs " + source_val.dump());
        }
        // If they are equal, no action needed, keep target value
    }
}


void ContextEngine::save_snapshot(const NodePath& key, const Context& ctx) {
    // Calculate size before adding
    size_t new_snapshot_size_kb = estimate_json_size_kb(ctx);

    // Check if adding this snapshot would exceed limits
    if (snapshots_.size() >= max_snapshots_ || (current_total_size_kb_ + new_snapshot_size_kb) > max_snapshot_size_kb_) {
        // Enforce budget before adding
        enforce_snapshot_budget();
        // Re-check after enforcement
        if (snapshots_.size() >= max_snapshots_ || (current_total_size_kb_ + new_snapshot_size_kb) > max_snapshot_size_kb_) {
            // Still over budget, cannot add. Log or ignore.
            std::cerr << "[WARNING] Cannot save snapshot, budget exceeded after enforcement. Key: " << key << std::endl;
            return;
        }
    }

    // Add snapshot
    snapshots_[key] = ctx; // Deep copy
    snapshot_order_.push_back(key);
    current_total_size_kb_ += new_snapshot_size_kb;
}

const Context* ContextEngine::get_snapshot(const NodePath& key) const {
    auto it = snapshots_.find(key);
    if (it != snapshots_.end()) {
        return &(it->second);
    }
    return nullptr; // Not found
}

void ContextEngine::enforce_snapshot_budget() {
    while (!snapshot_order_.empty() && (
               snapshots_.size() > max_snapshots_ ||
               current_total_size_kb_ > max_snapshot_size_kb_
           )) {
        NodePath oldest_key = snapshot_order_.front();
        snapshot_order_.erase(snapshot_order_.begin());

        auto it = snapshots_.find(oldest_key);
        if (it != snapshots_.end()) {
            current_total_size_kb_ -= estimate_json_size_kb(it->second);
            snapshots_.erase(it);
        }
    }
}

void ContextEngine::set_snapshot_limits(size_t max_count, size_t max_size_kb) {
    max_snapshots_ = max_count;
    max_snapshot_size_kb_ = max_size_kb;
    enforce_snapshot_budget(); // Apply new limits immediately
}

} // namespace agenticdsl

```
    
## `trace/include/trace/trace_exporter.h`
    
```cpp
// modules/trace/include/trace/trace_exporter.h
#ifndef AGENTICDSL_MODULES_TRACE_TRACE_EXPORTER_H
#define AGENTICDSL_MODULES_TRACE_TRACE_EXPORTER_H

#include "common/types/node.h"    // 引入 NodePath
#include "common/types/context.h" // 引入 Context
#include "common/types/budget.h"  // 引入 ExecutionBudget
#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <optional>
#include <chrono>

namespace agenticdsl {

struct TraceRecord {
    std::string trace_id;
    NodePath node_path;
    std::string type; // NodeType as string
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    std::string status; // "success", "failed", "skipped"
    std::optional<std::string> error_code;
    nlohmann::json context_delta; // 执行前后上下文的变化 (simplified)
    std::optional<NodePath> ctx_snapshot_key; // 关联的快照键 (v3.1)
    nlohmann::json budget_snapshot; // 执行时的预算状态
    nlohmann::json metadata; // 节点原始 metadata
    std::optional<std::string> llm_intent; // 从注释解析
    std::string mode; // "dev" or "prod"
    // Add other fields as needed per spec
};

class TraceExporter {
public:
    void on_node_start(
        const NodePath& path,
        NodeType type,
        const nlohmann::json& initial_context,
        const std::optional<ExecutionBudget>& budget
    );

    void on_node_end(
        const NodePath& path,
        const std::string& status,
        const std::optional<std::string>& error_code,
        const nlohmann::json& initial_context, // For calculating delta
        const nlohmann::json& final_context,
        const std::optional<NodePath>& snapshot_key, // v3.1
        const std::optional<ExecutionBudget>& budget
    );

    std::vector<TraceRecord> get_traces() const;
    void clear_traces();

private:
    std::vector<TraceRecord> traces_;
    std::string current_trace_id_ = "t-default"; // Should be generated uniquely per execution

    // Helper to calculate context delta (simplified)
    nlohmann::json calculate_context_delta(const nlohmann::json& initial, const nlohmann::json& final);
    // Helper to serialize budget state to JSON
    nlohmann::json serialize_budget_state(const std::optional<ExecutionBudget>& budget) const;
};

} // namespace agenticdsl

#endif // AGENTICDSL_MODULES_TRACE_TRACE_EXPORTER_H

```
    
## `trace/src/trace_exporter.cpp`
    
```cpp
// modules/trace/src/trace_exporter.cpp
#include "trace/trace_exporter.h"
#include <chrono>
#include <sstream>
#include <iomanip>

namespace agenticdsl {

void TraceExporter::on_node_start(
    const NodePath& path,
    NodeType type,
    const nlohmann::json& initial_context,
    const std::optional<ExecutionBudget>& budget) {

    TraceRecord record;
    record.trace_id = current_trace_id_;
    record.node_path = path;
    record.type = std::to_string(static_cast<int>(type)); // Convert enum to string representation
    record.start_time = std::chrono::system_clock::now();
    record.status = "running"; // Placeholder, will be updated in on_node_end
    record.context_delta = nlohmann::json::object(); // Initial delta is empty
    record.budget_snapshot = serialize_budget_state(budget);
    // record.metadata and record.llm_intent are not set here, only in on_node_end
    // record.ctx_snapshot_key is set in on_node_end

    traces_.push_back(std::move(record));
}

void TraceExporter::on_node_end(
    const NodePath& path,
    const std::string& status,
    const std::optional<std::string>& error_code,
    const nlohmann::json& initial_context,
    const nlohmann::json& final_context,
    const std::optional<NodePath>& snapshot_key,
    const std::optional<ExecutionBudget>& budget) {

    // Find the corresponding start record
    auto it = std::find_if(traces_.rbegin(), traces_.rend(),
                           [&path](const TraceRecord& r) { return r.node_path == path && r.status == "running"; });

    if (it != traces_.rend()) {
        TraceRecord& record = *it;
        record.end_time = std::chrono::system_clock::now();
        record.status = status;
        record.error_code = error_code;
        record.context_delta = calculate_context_delta(initial_context, final_context);
        record.ctx_snapshot_key = snapshot_key;
        record.budget_snapshot = serialize_budget_state(budget);
        // Note: metadata and llm_intent would ideally be captured during execution/node creation
        // For now, we assume they are not part of the execution flow captured here,
        // or are passed separately if needed. This example omits them for simplicity.
        record.mode = "dev"; // Should come from execution config
    }
}

std::vector<TraceRecord> TraceExporter::get_traces() const {
    return traces_;
}

void TraceExporter::clear_traces() {
    traces_.clear();
}

nlohmann::json TraceExporter::calculate_context_delta(const nlohmann::json& initial, const nlohmann::json& final) {
    // This is a simplified delta calculation.
    // A more robust implementation would recursively compare objects and arrays.
    nlohmann::json delta = nlohmann::json::object();
    for (auto it = final.begin(); it != final.end(); ++it) {
        const std::string& key = it.key();
        if (initial.contains(key)) {
            if (initial[key] != it.value()) {
                delta[key] = it.value(); // Value changed
            }
        } else {
            delta[key] = it.value(); // New key
        }
    }
    for (auto it = initial.begin(); it != initial.end(); ++it) {
        const std::string& key = it.key();
        if (!final.contains(key)) {
            delta[key] = nullptr; // Indicate deletion with null
        }
    }
    return delta;
}

nlohmann::json TraceExporter::serialize_budget_state(const std::optional<ExecutionBudget>& budget) const {
    if (!budget.has_value()) {
        return nlohmann::json::object(); // Return empty object if no budget
    }
    const auto& b = budget.value();
    nlohmann::json obj;
    obj["max_nodes"] = b.max_nodes;
    obj["max_llm_calls"] = b.max_llm_calls;
    obj["max_duration_sec"] = b.max_duration_sec;
    obj["nodes_used"] = b.nodes_used.load(); // Access atomic value
    obj["llm_calls_used"] = b.llm_calls_used.load(); // Access atomic value
    // Note: start_time is not serialized as it's a point-in-time value
    // Calculate and add elapsed time if needed
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - b.start_time).count();
    obj["elapsed_sec"] = elapsed;
    return obj;
}

} // namespace agenticdsl

```
    
## `scheduler/include/scheduler/execution_session.h`
    
```cpp
// modules/scheduler/include/scheduler/execution_session.h
#ifndef AGENTICDSL_MODULES_SCHEDULER_EXECUTION_SESSION_H
#define AGENTICDSL_MODULES_SCHEDULER_EXECUTION_SESSION_H

#include "common/types/context.h"
#include "common/types/node.h"
#include "common/types/budget.h"
#include "modules/context/context_engine.h"
#include "modules/budget/budget_controller.h"
#include "modules/trace/trace_exporter.h"
#include "modules/executor/node_executor.h"
#include "agenticdsl/tools/registry.h"
#include "agenticdsl/llm/llama_adapter.h"
#include "modules/parser/markdown_parser.h" // 引入 MarkdownParser (for GenerateSubgraph)
#include "modules/library/library_loader.h" // ← 新增：用于构建 available_subgraphs
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
        LlamaAdapter* llm_adapter = nullptr,
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

```
    
## `scheduler/include/scheduler/topo_scheduler.h`
    
```cpp
// modules/scheduler/include/scheduler/topo_scheduler.h
#ifndef AGENTICDSL_MODULES_SCHEDULER_TOPO_SCHEDULER_H
#define AGENTICDSL_MODULES_SCHEDULER_TOPO_SCHEDULER_H

#include "common/types/context.h" // 引入 Context
#include "common/types/node.h"    // 引入 NodePath, Node
#include "common/types/budget.h"  // 引入 ExecutionBudget
#include "scheduler/execution_session.h" // 引入 ExecutionSession
#include "agenticdsl/tools/registry.h" // 引入 ToolRegistry
#include "agenticdsl/llm/llama_adapter.h" // 引入 LlamaAdapter
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

    TopoScheduler(Config config, ToolRegistry& tool_registry, LlamaAdapter* llm_adapter = nullptr);

    void register_node(std::unique_ptr<Node> node);
    void build_dag(); // 构建依赖图
    ExecutionResult execute(Context initial_context);

    // Method for DSLEngine to call to add new graphs dynamically
    void append_dynamic_graphs(std::vector<ParsedGraph> new_graphs);

private:
    ExecutionSession session_;
    std::vector<std::unique_ptr<Node>> all_nodes_;
    std::unordered_map<NodePath, Node*> node_map_;
    std::unordered_map<NodePath, std::vector<NodePath>> reverse_edges_; // 后继 -> 前驱
    std::unordered_map<NodePath, int> in_degree_;
    std::queue<NodePath> ready_queue_;
    std::unordered_set<NodePath> executed_;
    std::vector<NodePath> call_stack_; // 用于 soft end
    //

    ResourceManager resource_manager_; // ← 成员变量，非全局单例
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

```
    
## `scheduler/include/scheduler/resource_manager.h`
    
```cpp
#ifndef AGENTICDSL_SCHEDULER_RESOURCE_MANAGER_H
#define AGENTICDSL_SCHEDULER_RESOURCE_MANAGER_H

#include "common/types.h"
#include <unordered_map>
#include <string>

namespace agenticdsl {

class ResourceManager {
public:
    void register_resource(const Resource& resource);
    bool has_resource(const NodePath& path) const;
    const Resource* get_resource(const NodePath& path) const;
    nlohmann::json get_resources_context() const;

private:
    std::unordered_map<NodePath, Resource> resources_;
};

} // namespace agenticdsl

#endif // AGENTICDSL_SCHEDULER_RESOURCE_MANAGER_H

```
    
## `scheduler/src/topo_scheduler.cpp`
    
```cpp
// modules/scheduler/src/topo_scheduler.cpp
#include "scheduler/topo_scheduler.h"
#include "common/utils/template_renderer.h"
#include <stdexcept>
#include <algorithm>
#include <set>
#include <queue>

namespace agenticdsl {

struct HardEndException : public std::exception {
    const char* what() const noexcept override {
        return "Hard end node encountered in branch, terminating main execution.";
    }
};

TopoScheduler::TopoScheduler(Config config, ToolRegistry& tool_registry, LlamaAdapter* llm_adapter)
    : session_(config.initial_budget, tool_registry, llm_adapter, resource_manager_, 
               [this](std::vector<ParsedGraph> graphs) { this->append_dynamic_graphs(std::move(graphs)); }) { // Pass callback to ExecutionSession
    // Initial budget is now handled by ExecutionSession
}

void TopoScheduler::register_node(std::unique_ptr<Node> node) {
    NodePath path = node->path;
    node_map_[path] = node.get();
    all_nodes_.push_back(std::move(node));
}

void TopoScheduler::register_resources() {
    for (const auto& node_ptr : all_nodes_) {
        if (node_ptr->type == NodeType::RESOURCE) {
            const ResourceNode* res_node = static_cast<const ResourceNode*>(node_ptr.get());
            Resource res{
                .path = res_node->path,
                .resource_type = res_node->resource_type,
                .uri = res_node->uri,
                .scope = res_node->scope,
                .metadata = res_node->metadata
            };
            resource_manager_.register_resource(res);
        }
    }
}

void TopoScheduler::build_dag() {
    register_resources();

    // 1. 初始化入度和反向边映射
    for (const auto& node_ptr : all_nodes_) {
        NodePath current_path = node_ptr->path;
        // Skip system nodes from main dependency calculation if desired
        // if (current_path.rfind("/__system__/", 0) == 0) continue;

        in_degree_[current_path] = 0;
        reverse_edges_[current_path] = {};
    }

    // 2. 构建依赖关系
    for (const auto& node_ptr : all_nodes_) {
        NodePath current_path = node_ptr->path;
        Node* node = node_ptr.get();

        // Handle 'next' dependencies
        for (const auto& next_path : node->next) {
            if (node_map_.count(next_path) == 0) {
                throw std::runtime_error("Next node not found: " + next_path);
            }
            // Add reverse edge: next depends on current
            reverse_edges_[next_path].push_back(current_path);
            // Increment in-degree of next
            in_degree_[next_path]++;
        }

        // Handle 'wait_for' dependencies (static dependencies defined at parse time)
        // This covers all_of, any_of, static arrays/strings
        if (node->metadata.contains("wait_for") && !node->metadata["wait_for"].is_string()) { // Only process if NOT a dynamic string
            const auto& wf = node->metadata["wait_for"];
            std::vector<NodePath> deps;

            // Parse wait_for structure
            if (wf.is_object()) {
                if (wf.contains("all_of")) {
                    const auto& all = wf["all_of"];
                    if (all.is_array()) {
                        for (const auto& item : all) deps.push_back(item.get<std::string>());
                    } else if (all.is_string()) {
                        deps.push_back(all.get<std::string>());
                    }
                }
                if (wf.contains("any_of")) {
                // Note: 'any_of' requires more complex scheduling logic (e.g., event-based)
                // For simplicity in a basic topo scheduler, we treat 'any_of' as 'all_of'
                // or handle it differently in execute_node. Here, we treat as all_of.
                // A full implementation would require a different scheduling model.
                    const auto& any = wf["any_of"];
                    if (any.is_array()) {
                        for (const auto& item : any) deps.push_back(item.get<std::string>());
                    } else if (any.is_string()) {
                        deps.push_back(any.get<std::string>());
                    }
                }
            } else if (wf.is_array()) {
                for (const auto& item : wf) deps.push_back(item.get<std::string>());
            } else if (wf.is_string()) {
                deps.push_back(wf.get<std::string>());
            }

            for (const auto& dep_path : deps) {
                if (node_map_.count(dep_path) == 0) {
                    throw std::runtime_error("wait_for dependency not found: " + dep_path);
                }
                // Add reverse edge: current depends on dep
                reverse_edges_[current_path].push_back(dep_path);
                // Increment in-degree of current
                in_degree_[current_path]++;
            }
        }
        // Dynamic wait_for (expressions resolved at runtime) is handled in execute_node loop
    }

    for (const auto& node_ptr : all_nodes_) {
        NodePath path = node_ptr->path;
        // Skip system nodes from initial ready queue if desired
        // if (path.rfind("/__system__/", 0) == 0) continue;

        if (in_degree_[path] == 0) {
            ready_queue_.push(path);
        }
    }
}

ExecutionResult TopoScheduler::execute(Context initial_context) {
    Context context = std::move(initial_context);

    while (!ready_queue_.empty() || !session_.get_pending_dynamic_deps().empty() || is_executing_fork_branches_) { // Continue while queue has items OR fork branches are running
        NodePath current_path;
        Node* current_node = nullptr;
        bool found_ready_node = false;

        if (is_executing_fork_branches_) {
             // If we are in the middle of executing fork branches, do that first.
             // This happens after a ForkNode sets the flag but before a JoinNode is encountered.
             // Or, if all branches are done, this loop handles the transition.
             execute_fork_branches(); // This will execute remaining branches if any
             if (current_fork_branch_index_ == current_fork_branches_.size()) {
                 // All branches finished, but JoinNode hasn't been encountered yet.
                 // This means the JoinNode is the next logical step, but it might not be in the ready_queue_ yet.
                 // The main loop will continue, and when it encounters the JoinNode, it will handle the merge.
                 // If the JoinNode is not reachable, it's an error.
                 // For now, assume JoinNode will be encountered.
                 finish_fork_simulation(); // Clean up fork state, but keep results
                 // The main loop will then pick up the JoinNode from the queue if it's there.
                 // If the JoinNode is *after* other nodes that depend on the fork results,
                 // the scheduler needs to handle this dependency correctly, which it should via `wait_for` or graph structure.
                 // The simulation assumes JoinNode comes after all branches logically (which is typical).
                 // If JoinNode is not in the queue, it means the graph structure might be wrong or `wait_for` is needed.
                 // Let's continue the main loop to see if JoinNode appears.
                 // If it doesn't, the final "unexecuted nodes" check will catch it.
                 std::cout << "[DEBUG] Fork branches done, waiting for JoinNode." << std::endl;
             }
             // If branches are still running, the loop will go to the next iteration.
             // If all branches are done, it will exit this `if` block and check the main queue.
        }

        // Check ready_queue_ first
        if (!ready_queue_.empty() && !is_executing_fork_branches_) {
            current_path = ready_queue_.front();
            ready_queue_.pop();
            found_ready_node = true;
        } else if (!ready_queue_.empty() && is_executing_fork_branches_ && current_fork_branch_index_ == current_fork_branches_.size()) {
            // No static ready nodes, check if any pending dynamic deps are now satisfied
            std::unordered_set<NodePath> dummy_executed; // Pass empty set if not tracking newly executed in this loop iteration
            session_.check_and_requeue_dynamic_deps(dummy_executed); // This will move nodes from pending to ready_queue_ if possible
            if (!ready_queue_.empty()) {
                current_path = ready_queue_.front();
                ready_queue_.pop();
                found_ready_node = true;
            }
        }

        if (!found_ready_node) {
            // If we couldn't find a ready node, but have pending dynamic deps,
            // it means we are waiting for a dependency that might never come.
            // This could be a deadlock or unmet condition.
            if (!session_.pending_dynamic_deps_.empty()) {
                 return {false, "Execution stopped: Unmet dynamic dependencies. Pending: " + nlohmann::json(session_.pending_dynamic_deps_).dump(), context, std::nullopt};
            }
            break; // No more nodes to execute
        }


        // Skip if already executed (shouldn't happen in strict topo, but good check)
        if (executed_.count(current_path) > 0) {
            continue;
        }

        auto node_it = node_map_.find(current_path);
        if (node_it == node_map_.end()) {
            return {false, "Node not found in map: " + current_path, context, std::nullopt};
        }
        current_node = node_it->second;

        // --- v3.1: Handle Dynamic wait_for (resolved during execution) ---
        bool can_execute = true;
        if (current_node->metadata.contains("wait_for") && node->metadata["wait_for"].is_string()) {
            // This is a dynamic wait_for expression
            std::string dynamic_expr = node->metadata["wait_for"].get<std::string>();

            try {
                std::string rendered_deps_str = InjaTemplateRenderer::render(dynamic_expr, context);
                // Assume rendered result is a JSON array of node paths
                auto rendered_deps_json = nlohmann::json::parse(rendered_deps_str);
                std::vector<NodePath> rendered_deps;
                if (rendered_deps_json.is_array()) {
                    for (const auto& item : rendered_deps_json) {
                        if (item.is_string()) {
                            rendered_deps.push_back(item.get<std::string>());
                        }
                    }
                } else if (rendered_deps_json.is_string()) {
                    rendered_deps.push_back(rendered_deps_json.get<std::string>());
                } else {
                    // If not array/string after rendering, treat as single path or error
                    // For this example, assume it's a single path string
                    rendered_deps.push_back(rendered_deps_str);
                }

                // Check if all dynamic dependencies are executed
                for (const auto& dep_path : rendered_deps) {
                    if (executed_.count(dep_path) == 0) {
                        can_execute = false;
                        // Put back on queue or wait? For topo, we might need a different model for dynamic deps.
                        // For now, just put it back on the ready queue for now, assuming it will become ready later.
                        // This can lead to busy waiting if dependency is never met.
                        // A more robust system would track pending dynamic dependencies separately.
                        ready_queue_.push(current_path);
                        break; // Stop checking deps for this node
                    }
                }
            } catch (const std::exception& e) {
                return {false, "Failed to resolve dynamic wait_for for node '" + current_path + "': " + e.what(), context, std::nullopt};
            }

        }

        if (!can_execute) {
            continue;
        }

        // --- Execute Node via ExecutionSession ---
        // Check node type here
        //if (current_node->type == NodeType::FORK || current_node->type == NodeType::GENERATE_SUBGRAPH) {
        //    session_.context_engine_.save_snapshot(current_path, context); // Accessing private member via friend
        //}

        auto session_result = session_.execute_node(current_node, context);

        if (!session_result.success) {
             if (session_result.message.find("Jumping to:") != std::string::npos) {
                // Extract jump target (simplified)
                size_t pos = session_result.message.find("Jumping to:");
                if (pos != std::string::npos) {
                    NodePath target = session_result.message.substr(pos + 12); // "Jumping to: " is 12 chars
                    std::cout << "[DEBUG] Node " << current_path << " failed assert, jumping to " << target << std::endl;
                    // Clear queue and add jump target
                    std::queue<NodePath> empty_queue;
                    ready_queue_.swap(empty_queue);
                    ready_queue_.push(target);
                    continue; // Continue loop to execute the jump target
                }
            }
            return {false, session_result.message, context, session_result.paused_at};
        }

        // Update context with result from session
        context = std::move(session_result.new_context);

        // Mark as executed
        executed_.insert(current_path);

        if (current_node->type == NodeType::FORK) {
            const ForkNode* fork_node = dynamic_cast<const ForkNode*>(current_node);
            if (!fork_node) {
                throw std::runtime_error("Node type FORK but not ForkNode instance");
            }
             // After ForkNode execution (and snapshot), start simulation
             start_fork_simulation(dynamic_cast<const ForkNode*>(current_node), context); // Pass current context which is the snapshot context at this point due to ExecutionSession logic
             // The main execution loop will then call execute_fork_branches in the next iteration
             continue; // Go to next loop iteration to handle branches
        }

        if (current_node->type == NodeType::JOIN) {
             // Check if all corresponding fork branches are done
             if (is_executing_fork_branches_ && current_fork_branch_index_ == current_fork_branches_.size()) {
                 // All branches finished, now process the join
                 start_join_simulation(dynamic_cast<const JoinNode*>(current_node));
                 finish_join_simulation(context); // Merge results into main context
                 finish_fork_simulation(); // Clean up fork state (results were used)
                 std::cout << "[DEBUG] Join completed, merged context." << std::endl;
                 // Continue with main execution flow using the merged context
             } else {
                 // JoinNode encountered before all branches are done - This is an error or requires complex waiting logic
                 // In this simplified simulation, we assume JoinNode comes after all branches are logically processed.
                 // If branches are still running, the main loop should wait until they finish (handled by is_executing_fork_branches_ flag).
                 // If branches are done but Join is encountered, the logic above handles it.
                 // If branches are not done, this node should wait. The loop will come back to it.
                 std::cout << "[DEBUG] JoinNode " << current_path << " encountered, waiting for branches to finish." << std::endl;
                 ready_queue_.push(current_path); // Re-queue JoinNode to check again later
                 continue; // Go to next loop iteration (likely processing next branch or another node)
             }
        }

        // Check for pause (e.g., LLM call)
        if (session_result.paused_at.has_value()) {
            return {true, "Paused at LLM call", context, session_result.paused_at};
        }

        // Handle END node termination
        if (current_node->type == NodeType::END) {
            std::string mode = current_node->metadata.value("termination_mode", "hard");
            if (mode == "hard" && !is_executing_fork_branches_) {
                break; // Terminate entire flow
            }
            // Soft end: continue scheduling, but might pop call_stack_ in a more complex impl
        }

        if (!dynamic_graphs_.empty()) {
             // Load new graphs into the scheduler's data structures
             load_graphs(std::vector<ParsedGraph>()); // This is a hack to clear and reload. Better: add nodes incrementally.
             // A better way: have append_dynamic_graphs call register_node and update in_degree_ directly.
             // For now, let's just add nodes and rebuild the relevant parts.
             // Let's assume load_graphs can handle appending new nodes correctly.
             // We need to call build_dag again to incorporate new nodes and dependencies.
             // This is expensive. A better approach is incremental DAG update.
             // For this example, we'll rebuild. In practice, incremental updates are preferred.
             // Let's clear the current structures and reload everything (including dynamic ones).
             // This is inefficient but demonstrates the concept.
             std::vector<std::unique_ptr<Node>> all_nodes_copy; // Create a new list to avoid modifying while iterating
             for (auto& n : all_nodes_) {
                 all_nodes_copy.push_back(n->clone()); // Clone existing nodes
             }
             // Register dynamic nodes
             for (const auto& graph : dynamic_graphs_) {
                 for (const auto& node_ptr : graph.nodes) {
                     if (node_ptr) {
                         all_nodes_copy.push_back(node_ptr->clone());
                     }
                 }
             }
             // Clear old structures
             node_map_.clear();
             reverse_edges_.clear();
             in_degree_.clear();
             // Rebuild DAG with all nodes (existing + dynamic)
             all_nodes_ = std::move(all_nodes_copy);
             build_dag(); // This will recreate ready_queue_ with new nodes if their deps are met
             dynamic_graphs_.clear(); // Clear the list after processing
             // Re-check the current node's dependencies after rebuild
             // If current node's deps are now satisfied, it might be added back to ready_queue_ by build_dag
             // If not, it will be skipped in this loop iteration.
             // We need to ensure the main loop continues correctly.
             // This approach is disruptive. A better scheduler would handle this more gracefully.
             // Let's just continue the loop. The current node might be re-queued by build_dag if its deps are now ready.
             // Or, we can restart the main loop logic from the beginning.
             // For simplicity in this example, let's just continue the loop.
             // The ready_queue_ might now contain nodes that were previously unready.
             // This is the most straightforward way to handle the rebuild.
        }

        // Update successors' in-degrees and add to ready queue if ready
        for (const auto& next_path : current_node->next) {
            if (--in_degree_[next_path] == 0) {
                ready_queue_.push(next_path);
            }
        }

        // Check if any pending dynamic deps are now satisfied due to this execution
        std::unordered_set<NodePath> newly_executed = {current_path};
        session_.check_and_requeue_dynamic_deps(newly_executed);
    }

    // Check if execution stopped due to budget
    if (session_.is_budget_exceeded()) {
        return {false, "Execution stopped: Budget exceeded", context, std::nullopt};
    }

    // Final check for unexecuted nodes
    std::unordered_set<NodePath> all_node_paths;
    for (const auto& n : all_nodes_) {
        all_node_paths.insert(n->path);
    }
    std::set<NodePath> unexecuted;
    std::set_difference(all_node_paths.begin(), all_node_paths.end(),
                        executed_.begin(), executed_.end(),
                        std::inserter(unexecuted, unexecuted.begin()));

    if (!unexecuted.empty()) {
        return {false, "Execution stopped: Unmet dependencies or cycles. Unexecuted nodes: " + nlohmann::json(unexecuted).dump(), context, std::nullopt};
    }

    return {true, "Execution completed successfully", context, std::nullopt};
}

void TopoScheduler::append_dynamic_graphs(std::vector<ParsedGraph> new_graphs) {
    // Store the new graphs temporarily
    // In a more complex system, this might trigger an event or flag for the main loop
    dynamic_graphs_.insert(dynamic_graphs_.end(), std::make_move_iterator(new_graphs.begin()), std::make_move_iterator(new_graphs.end()));
    // The main execute loop will check this list and rebuild the DAG if necessary.
}

void TopoScheduler::start_fork_simulation(const ForkNode* fork_node, const Context& fork_context_snapshot) {
    current_fork_node_path_ = fork_node->path;
    current_fork_branches_ = fork_node->branches; // Store the branches to execute
    current_fork_branch_results_.clear(); // Clear previous results if any
    current_fork_branch_index_ = 0;
    is_executing_fork_branches_ = true;
    // The fork_context_snapshot is already saved by ExecutionSession
    // We just need to remember the branches to execute.
    std::cout << "[DEBUG] Started fork simulation for node " << fork_node->path << " with " << current_fork_branches_.size() << " branches." << std::endl;
}

void TopoScheduler::execute_fork_branches() {
    if (!is_executing_fork_branches_ || current_fork_branches_.empty()) return;

    const Context* fork_snapshot = session_.get_context_engine().get_snapshot(current_fork_node_path_.value());
    if (!fork_snapshot) {
        throw std::runtime_error("Snapshot for fork node not found: " + current_fork_node_path_.value());
    }

    // Execute branches sequentially
    while (current_fork_branch_index_ < current_fork_branches_.size()) {
        const NodePath& branch_path = current_fork_branches_[current_fork_branch_index_];
        std::cout << "[DEBUG] Executing fork branch: " << branch_path << std::endl;

        // 1. Restore snapshot for this branch
        Context branch_initial_ctx = *fork_snapshot; // Copy the snapshot

        try {
            // 2. Execute the branch subgraph
            Context branch_final_ctx = execute_single_branch(branch_path, branch_initial_ctx);

            // 3. Store result
            current_fork_branch_results_.push_back(std::move(branch_final_ctx));
            current_fork_branch_index_++;
        } catch (const HardEndException& e) {
            // Re-throw to be caught in the main execute loop
            throw;
        }

        std::cout << "[DEBUG] Branch " << branch_path << " completed. Result stored. Branch " << current_fork_branch_index_ << " / " << current_fork_branches_.size() << " done." << std::endl;
    }

    // All branches executed, simulation phase is done for fork
    // The join logic will be handled when the corresponding JoinNode is encountered
    std::cout << "[DEBUG] All fork branches completed. Ready for join." << std::endl;
}
Context TopoScheduler::execute_single_branch(const NodePath& branch_path, const Context& initial_context) {
    // This is a simplified way to execute a subgraph path.
    // A more robust way would be to have a separate scheduler instance or a recursive execution mechanism
    // that only operates within the scope of the given branch_path.
    // For now, let's assume the branch_path is the root of a subgraph
    // and we find its starting node (e.g., the first node in the graph with that path prefix that is a start node or the first node if no explicit start).

    // A simple heuristic: Find the first node in the current node_map_ that starts with the branch_path.
    // This assumes the branch_path corresponds to a subgraph defined in the parsed DSL.
    // In a more complex system, the branch_path might point to a specific starting node ID within that subgraph.
    // For simplicity here, we assume the first node matching the path prefix is the entry point.
    NodePath start_node_path;
    for (const auto& [path, node] : node_map_) {
        if (path.rfind(branch_path + "/", 0) == 0 || path == branch_path) { // Check if path starts with branch_path
             // For a true subgraph like `/lib/reasoning/logic_branch`, the first node might be `/lib/reasoning/logic_branch/step1`
             // We assume the first such node found is the start of the branch execution.
             // A more robust system would have a dedicated 'start' node or an explicit entry point definition.
             start_node_path = path;
             break;
        }
    }

    if (start_node_path.empty()) {
        throw std::runtime_error("No starting node found for branch path: " + branch_path);
    }

    std::cout << "[DEBUG] Found start node for branch " << branch_path << ": " << start_node_path << std::endl;

    // --- Execute the subgraph starting from start_node_path ---
    // This requires temporarily modifying the scheduler's state (queue, executed set) to run only this subgraph.
    // This is complex. A simpler approach is to assume the subgraph is a self-contained piece that can be executed
    // using the main scheduler's logic, but with a different starting point and potentially stopping conditions.

    // For this simulation, let's create a temporary execution loop *within* this function.
    // This loop will only process nodes related to the branch_path.
    // We need a way to identify when the branch execution ends (e.g., hits an END node with termination_mode soft/hard for this subgraph context).

    // A placeholder: This requires significant logic to execute a subgraph in isolation.
    // It involves finding the entry point, managing its own ready queue based on its internal dependencies,
    // and stopping when it reaches an END node or a node that points back to the parent graph (which is JoinNode in this case).
    // This is essentially implementing subgraph execution within the main scheduler.

    // A more practical approach for simulation might be:
    // 1. Have a method that finds the entry node for the branch_path.
    // 2. Execute nodes reachable from that entry node until an END node is hit, collecting the final context.
    // 3. Be careful not to execute nodes outside the branch_path scope (or handle them as external dependencies if they exist and are already executed).

    // For now, let's assume a method exists or the logic is embedded here.
    // This is a complex part. Let's assume `execute_subgraph_from_path` exists.
    // return execute_subgraph_from_path(start_node_path, initial_context);

    // Placeholder implementation - This is NOT correct and needs a proper subgraph execution mechanism.
    // It's just to show where the logic would go.
    Context current_ctx = initial_context;
    std::queue<NodePath> branch_ready_queue;
    std::unordered_set<NodePath> branch_executed;
    std::unordered_map<NodePath, int> branch_in_degree = in_degree_; // Copy global in_degree
    std::unordered_map<NodePath, std::vector<NodePath>> branch_reverse_edges = reverse_edges_; // Copy global edges

    // Find initial ready nodes *within* the branch scope
    for (const auto& [path, node] : node_map_) {
        if (path.rfind(branch_path + "/", 0) == 0 || path == branch_path) { // Node belongs to branch
             if (branch_in_degree[path] == 0) {
                 branch_ready_queue.push(path);
             }
        }
    }

    while (!branch_ready_queue.empty()) {
        NodePath current_path = branch_ready_queue.front();
        branch_ready_queue.pop();

        if (branch_executed.count(current_path) > 0) continue;

        auto node_it = node_map_.find(current_path);
        if (node_it == node_map_.end()) {
            throw std::runtime_error("Node not found in map during branch execution: " + current_path);
        }
        Node* node = node_it->second;

        // Execute the node using the session
        auto session_result = session_.execute_node(node, current_ctx);
        if (!session_result.success) {
            // Handle errors within the branch execution
            throw std::runtime_error("Branch execution failed at " + current_path + ": " + session_result.message);
        }
        current_ctx = std::move(session_result.new_context);
        branch_executed.insert(current_path);

        // Check for END node to potentially stop this branch
        if (node->type == NodeType::END) {
             std::string mode = node->metadata.value("termination_mode", "hard");
             if (mode == "soft") {
                 // For soft end in a branch context, it might mean returning the context up to join
                 // For hard end, it might terminate the whole branch execution loop here.
                 // Let's assume soft means this branch is done.
                 break; // Exit branch execution loop
             } else {
                throw HardEndException(); // Propagate hard end to main loop
             }
             // Hard end might terminate the whole DAG if not handled carefully in subgraph context.
             // For simulation, we might just break the branch loop on any END.
             break;
        }

        // Update successors' in-degrees and add to branch queue if ready
        for (const auto& next_path : node->next) {
            // Only update if the successor is also part of the same branch
            if (node_map_.count(next_path) > 0 && (next_path.rfind(branch_path + "/", 0) == 0 || next_path == branch_path)) {
                 if (--branch_in_degree[next_path] == 0) {
                     branch_ready_queue.push(next_path);
                 }
            }
        }
    }

    return current_ctx; // Return the final context of the branch execution
}


void TopoScheduler::finish_fork_simulation() {
    // Fork simulation is considered finished when all branches are executed (handled in execute_fork_branches).
    // This function can be used to clean up state if needed after all branches finish.
    is_executing_fork_branches_ = false;
    std::cout << "[DEBUG] Finished fork simulation for node " << current_fork_node_path_.value() << std::endl;
    current_fork_node_path_.reset();
    current_fork_branches_.clear();
    // current_fork_branch_results_ is kept until join is processed
}

void TopoScheduler::start_join_simulation(const JoinNode* join_node) {
    current_join_node_path_ = join_node->path;
    join_merge_strategy_ = join_node->merge_strategy; // Store the strategy from the JoinNode
    // join_wait_for_ might be used if JoinNode has explicit dependencies beyond fork branches
    if (join_node->wait_for.empty()) {
        // Default behavior: wait for all branches from the corresponding Fork
        // This requires tracking which Fork this Join corresponds to.
        // For simplicity, assume the last finished Fork corresponds to this Join.
        // A more robust system would explicitly link Fork and Join nodes.
        // For now, we rely on the fact that all branches from the current fork are collected.
    } else {
        join_wait_for_ = join_node->wait_for; // Use explicit dependencies if provided
    }
    std::cout << "[DEBUG] Started join simulation for node " << join_node->path << " with strategy " << join_merge_strategy_ << std::endl;
}

void TopoScheduler::finish_join_simulation(Context& main_context) {
    if (current_fork_branch_results_.size() != current_fork_branches_.size()) {
        throw std::runtime_error("JoinNode: Not all fork branches have results for merging.");
    }

    std::cout << "[DEBUG] Merging " << current_fork_branch_results_.size() << " branch results using strategy: " << join_merge_strategy_ << std::endl;

    if (!current_fork_branch_results_.empty()) {
        // Apply merge strategy iteratively
        ContextMergePolicy policy;
        policy.default_strategy = join_merge_strategy_;
        for (const auto& branch_ctx : current_fork_branch_results_) {
             ContextEngine::merge(main_context, branch_ctx, policy);
        }
    }

    // Clean up fork/join state
    current_join_node_path_.reset();
    current_fork_branch_results_.clear();
    join_wait_for_.clear();
    std::cout << "[DEBUG] Finished join simulation for node " << current_join_node_path_.value() << std::endl;
}

void TopoScheduler::load_graphs(const std::vector<std::unique_ptr<Node>>& nodes) {
    // This method should register nodes and prepare for DAG building.
    // It's likely called during initial setup.
    // For dynamic loading, we use append_dynamic_graphs and rebuild DAG.
    // This method might need to be refactored or called differently for dynamic graphs.
    // For the current example, it's implicitly handled by the execute loop when dynamic_graphs_ is not empty.
    // If needed, add logic here to append nodes incrementally.
    for (const auto& node_ptr : nodes) {
        register_node(node_ptr->clone()); // Use clone to avoid moving out of the vector if it's const
    }
    // build_dag(); // Only call if this is an initial load, not for dynamic append
}


} // namespace agenticdsl

```
    
## `scheduler/src/execution_session.cpp`
    
```cpp
// modules/scheduler/src/execution_session.cpp
#include "scheduler/execution_session.h"
#include "common/utils/template_renderer.h" // 引入 InjaTemplateRenderer (for Trace context delta)
//#include "agenticdsl/llm/prompt_builder.h" // 引入 PromptBuilder
#include <stdexcept>
#include <algorithm> // For std::find

namespace agenticdsl {

ExecutionSession::ExecutionSession(
    std::optional<ExecutionBudget> initial_budget,
    ToolRegistry& tool_registry,
    LlamaAdapter* llm_adapter,
    ResourceManager& resource_manager, // ← 新增
    const std::vector<ParsedGraph>* full_graphs,
    AppendGraphsCallback append_graphs_callback)
    : budget_controller_(initial_budget),
      node_executor_(tool_registry, llm_adapter),
      resource_manager_(resource_manager), // ← 初始化
      full_graphs_(full_graphs),
      append_graphs_callback_(std::move(append_graphs_callback)) { // Store callback

    node_executor_.set_append_graphs_callback(append_graphs_callback_);
    // 这里使用默认值，实际应用中可能需要更灵活的配置
    context_engine_.set_snapshot_limits(10, 512); // dev default
}
nlohmann::json ExecutionSession::build_available_subgraphs_context() const {
    nlohmann::json libs = nlohmann::json::array();
    
    // 1. 静态标准库（/lib/**）
    auto& loader = StandardLibraryLoader::instance();
    for (const auto& entry : loader.get_available_libraries()) {
        if (entry.is_subgraph) {
            nlohmann::json lib;
            lib["path"] = entry.path;
            if (!entry.output_schema.is_null()) {
                lib["signature"] = {
                    {"outputs", entry.output_schema}
                };
            }
            lib["permissions"] = entry.permissions;
            lib["stability"] = "stable";
            libs.push_back(std::move(lib));
        }
    }
    
    // 2. 动态生成子图（/dynamic/**）
    if (full_graphs_) {
        for (const auto& graph : *full_graphs_) {
            if (graph.path.rfind("/dynamic/", 0) == 0 && !graph.output_schema.is_null()) {
                nlohmann::json lib;
                lib["path"] = graph.path;
                lib["signature"] = {
                    {"outputs", graph.output_schema}
                };
                lib["permissions"] = graph.permissions;
                lib["stability"] = "dynamic";
                libs.push_back(std::move(lib));
            }
        }
    }
    
    return libs;
}

std::string ExecutionSession::inject_subgraphs_into_prompt(
    const std::string& base_prompt,
    const Context& context) const {
    Context ctx = context;
    ctx["available_subgraphs"] = build_available_subgraphs_context();
    return InjaTemplateRenderer::render(base_prompt, ctx);
}


ExecutionSession::ExecutionResult ExecutionSession::execute_node(Node* node, const Context& initial_context) {
    ExecutionResult result;
    result.success = true;
    result.message = "Node executed successfully";

    Context context_with_resources = initial_context;
    auto resources_ctx = resource_manager_.get_resources_context(); // ← 需要 ExecutionSession 持有 resource_manager_
    if (!resources_ctx.empty()) {
        context_with_resources["resources"] = std::move(resources_ctx);
    }

    // v3.1: Check for snapshot trigger BEFORE execution
    bool snapshot_needed = needs_snapshot(node);
    if (snapshot_needed) {
        context_engine_.save_snapshot(node->path, context_with_resources); // Snapshot *before* execution
        result.snapshot_key = node->path;
    }

    // 1. 检查预算
    if (node->type == NodeType::LLM_CALL) {
        if (!budget_controller_.try_consume_llm_call()) {
            result.success = false;
            result.message = "Budget exceeded: LLM call limit reached";
            return result;
        }
    }
    // v3.1: Check for subgraph depth budget for GENERATE_SUBGRAPH
    if (node->type == NodeType::GENERATE_SUBGRAPH) {
        if (!budget_controller_.try_consume_subgraph_depth()) { // Assume BudgetController has this method
            result.success = false;
            result.message = "Budget exceeded: Subgraph depth limit reached";
            return result;
        }
    }
    if (!budget_controller_.try_consume_node()) {
        result.success = false;
        result.message = "Budget exceeded: Node limit reached";
        return result;
    }

    // 2. 记录 Trace 开始
    nlohmann::json initial_ctx_json = context_with_resources;
    trace_exporter_.on_node_start(node->path, node->type, initial_ctx_json, budget_controller_.get_budget());

    // 3. 执行节点
    try {
        // 统一执行路径
        auto execution_result = context_engine_.execute_with_snapshot(
            [this, node](const Context& ctx) {
                // 对于 GENERATE_SUBGRAPH，注入 available_subgraphs
                if (node->type == NodeType::GENERATE_SUBGRAPH) {
                    const GenerateSubgraphNode* gsn = static_cast<const GenerateSubgraphNode*>(node);
                    std::string rendered_prompt = this->inject_subgraphs_into_prompt(gsn->prompt_template, ctx);
                    Context new_ctx = ctx;
                    new_ctx["__rendered_prompt__"] = rendered_prompt; // 临时存储
                    return node_executor_.execute_node(node, new_ctx);
                }
                return node_executor_.execute_node(node, ctx);
            },
            context_with_resources,
            snapshot_needed,
            node->path
        );

        result.new_context = std::move(execution_result.new_context);
        result.snapshot_key = execution_result.snapshot_key;
/*
        if (node->type == NodeType::GENERATE_SUBGRAPH) {
            // Use the callback-aware execution method
            result.new_context = execute_generate_subgraph_with_callback(dynamic_cast<const GenerateSubgraphNode*>(node), context_with_resources);
        } else {
            result.new_context = node_executor_.execute_node(node, context_with_resources);
        }
        // Note: Snapshot key is set above if needed_snapshot was true.
        // */

        // --- v3.1: Check for LLM Call Pause ---
        if (node->type == NodeType::LLM_CALL) {
             result.paused_at = node->path;
             // For this synchronous executor, we just return here.
             // An async executor would handle pausing differently.
        }

    } catch (const std::exception& e) {
        result.success = false;
        result.message = std::string("Node execution failed: ") + e.what();
    }

    // 4. 记录 Trace 结束
    nlohmann::json final_ctx_json = result.new_context;
    trace_exporter_.on_node_end(
        node->path,
        result.success ? "success" : "failed",
        result.success ? std::nullopt : std::make_optional(result.message),
        initial_ctx_json,
        final_ctx_json,
        result.snapshot_key,
        budget_controller_.get_budget()
    );

    return result;
}
void ExecutionSession::check_and_requeue_dynamic_deps(const std::unordered_set<NodePath>& newly_executed_nodes) {
    std::vector<NodePath> satisfied_nodes;
    for (auto it = pending_dynamic_deps_.begin(); it != pending_dynamic_deps_.end();) {
        NodePath node_path = it->first;
        std::vector<NodePath>& deps = it->second;

        // Remove deps that were just executed
        deps.erase(std::remove_if(deps.begin(), deps.end(),
                                  [&newly_executed_nodes](const NodePath& dep) {
                                      return newly_executed_nodes.count(dep) > 0;
                                  }),
                   deps.end());

        if (deps.empty()) {
            // All dependencies for this node are now satisfied
            satisfied_nodes.push_back(node_path);
            it = pending_dynamic_deps_.erase(it); // Remove from pending list
        } else {
            ++it;
        }
    }

}

bool ExecutionSession::is_budget_exceeded() const {
    return budget_controller_.exceeded();
}

const TraceExporter& ExecutionSession::get_trace_exporter() const {
    return trace_exporter_;
}

const BudgetController& ExecutionSession::get_budget_controller() const {
    return budget_controller_;
}

const ContextEngine& ExecutionSession::get_context_engine() const {
    return context_engine_;
}

bool ExecutionSession::needs_snapshot(Node* node) const {
    // v3.1: Automatic snapshot triggers
    if (node->metadata.contains("snapshot_before_execution") && 
        node->metadata["snapshot_before_execution"].get<bool>()) {
        return true;
    }

    // Check for a hypothetical 'assert' node type (requires enum update)
    // if (node->type == NodeType::ASSERT) { // Assuming this enum value exists
    //     return true;
    // }

    // Check for tool_call with rollback flag (requires metadata field)
    if (node->type == NodeType::TOOL_CALL) {
        if (node->metadata.contains("rollback_on_failure") && node->metadata["rollback_on_failure"].get<bool>()) {
            return true;
        }
    }

    // v3.1: Fork, GenerateSubgraph, Assert always trigger
    if (node->type == NodeType::FORK || node->type == NodeType::GENERATE_SUBGRAPH || node->type == NodeType::ASSERT /* Assume assert exists */) {
        return true;
    }
    return false;
}

std::vector<NodePath> ExecutionSession::parse_dynamic_wait_for(const nlohmann::json& expr, const Context& ctx) {
    std::vector<NodePath> result;
    if (expr.is_string()) {
        std::string rendered = InjaTemplateRenderer::render(expr.get<std::string>(), ctx);
        try {
            auto arr = nlohmann::json::parse(rendered);
            if (arr.is_array()) {
                for (const auto& item : arr) {
                    if (item.is_string()) result.push_back(item.get<std::string>());
                }
            } else if (arr.is_string()) {
                 result.push_back(arr.get<std::string>()); // Single path
            }
        } catch (...) {
            // If parsing fails, treat as single path
            result.push_back(rendered);
        }
    } else if (expr.is_array()) {
        for (const auto& item : expr) {
            if (item.is_string()) {
                result.push_back(InjaTemplateRenderer::render(item.get<std::string>(), ctx));
            }
        }
    }
    return result;
}


} // namespace agenticdsl

```
    
## `scheduler/src/resource_manager.cpp`
    
```cpp
#include "agenticdsl/scheduler/resource_manager.h"
#include <stdexcept>

namespace agenticdsl {

void ResourceManager::register_resource(const Resource& resource) {
    resources_[resource.path] = resource;
}

bool ResourceManager::has_resource(const NodePath& path) const {
    return resources_.find(path) != resources_.end();
}

const Resource* ResourceManager::get_resource(const NodePath& path) const {
    auto it = resources_.find(path);
    return (it != resources_.end()) ? &it->second : nullptr;
}

nlohmann::json ResourceManager::get_resources_context() const {
    nlohmann::json resources_ctx = nlohmann::json::object();
    for (const auto& [path, resource] : resources_) {
        nlohmann::json resource_info;
        resource_info["uri"] = resource.uri;
        resource_info["type"] = static_cast<int>(resource.resource_type);
        resource_info["scope"] = resource.scope;
        resources_ctx[path] = resource_info;
    }
    return resources_ctx;
}

} // namespace agenticdsl

```
    
## `library/include/library/library_loader.h`
    
```cpp
// modules/library/include/library/library_loader.h
#ifndef AGENTICDSL_MODULES_LIBRARY_LIBRARY_LOADER_H
#define AGENTICDSL_MODULES_LIBRARY_LIBRARY_LOADER_H

#include "library/schema.h" // 引入 LibraryEntry
#include "modules/parser/markdown_parser.h" // 引入 ParsedGraph
#include <vector>
#include <string>

namespace agenticdsl {

class StandardLibraryLoader {
public:
    static StandardLibraryLoader& instance();
    const std::vector<LibraryEntry>& get_available_libraries() const;
    void load_from_directory(const std::string& lib_dir);
    void load_builtin_libraries(); // 加载内置子图定义（路径、Schema）

private:
    StandardLibraryLoader() = default;
    std::vector<LibraryEntry> libraries_;
    MarkdownParser parser_; // 内部使用 parser
};

} // namespace agenticdsl

#endif // AGENTICDSL_MODULES_LIBRARY_LIBRARY_LOADER_H

```
    
## `library/include/library/schema.h`
    
```cpp
// modules/library/include/library/schema.h
#ifndef AGENTICDSL_MODULES_LIBRARY_SCHEMA_H
#define AGENTICDSL_MODULES_LIBRARY_SCHEMA_H

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp> // For storing JSON Schema

namespace agenticdsl {

struct LibraryEntry {
    std::string path;
    std::optional<std::string> signature; // Original signature string
    std::optional<nlohmann::json> output_schema; // Parsed JSON Schema from signature (v3.1)
    std::vector<std::string> permissions;
    bool is_subgraph = false;
};

} // namespace agenticdsl

#endif // AGENTICDSL_MODULES_LIBRARY_SCHEMA_H

```
    
## `library/src/library_loader.cpp`
    
```cpp
// modules/library/src/library_loader.cpp
#include "library/library_loader.h"
#include "agenticdsl/core/system_nodes.h" // For create_system_nodes if needed, or define built-ins differently
#include <filesystem>
#include <fstream>
#include <sstream>

namespace agenticdsl {

StandardLibraryLoader& StandardLibraryLoader::instance() {
    static StandardLibraryLoader loader;
    static bool initialized = false;
    if (!initialized) {
        loader.load_builtin_libraries();
        // Optional: loader.load_from_directory("./lib");
        initialized = true;
    }
    return loader;
}

void StandardLibraryLoader::load_builtin_libraries() {
    // Register /lib/utils/noop (defined as system node, but conceptually a library)
    // libraries_.push_back({
    //      "/lib/utils/noop",
    //     std::nullopt,
    //     {},
    //     false // is single node
    // });

    // Example: Register /lib/math/add
    // This is just a declaration, actual execution requires the full graph.
    // The graph for /lib/math/add would be loaded separately or defined in a .md file.
    // For v3.1, we can define a signature.
    libraries_.push_back({
         "/lib/math/add",
         "(a: number, b: number) -> {sum: number}",
         // Parsed schema would be calculated by the parser/loader when the actual graph is loaded
         // For built-ins, we might hardcode the schema or parse the signature string here.
         nlohmann::json::parse(R"({"type": "object", "properties": {"sum": {"type": "number"}}})"), // Example schema
         {},
         true // is subgraph
    });

    // Add other built-in library entries as needed per v3.1 spec
    libraries_.push_back({
         "/lib/reasoning/with_rollback",
         "(try_path: string, fallback_path: string) -> {success: boolean}",
         nlohmann::json::parse(R"({"type": "object", "properties": {"success": {"type": "boolean"}}})"), // Example schema
         {},
         true // is subgraph
    });

    // ... add more ...
}

const std::vector<LibraryEntry>& StandardLibraryLoader::get_available_libraries() const {
    return libraries_;
}

void StandardLibraryLoader::load_from_directory(const std::string& lib_dir) {
    namespace fs = std::filesystem;
    if (!fs::exists(lib_dir) || !fs::is_directory(lib_dir)) return;

    for (const auto& entry : fs::recursive_directory_iterator(lib_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".md") {
            std::ifstream file(entry.path());
            std::stringstream buffer;
            buffer << file.rdbuf();
            try {
                auto graphs = parser_.parse_from_string(buffer.str());
                for (const auto& g : graphs) {
                    if (g.is_standard_library) {
                        LibraryEntry entry;
                        entry.path = g.path;
                        entry.signature = g.signature;
                        entry.output_schema = g.output_schema; // From parser (v3.1)
                        entry.permissions = g.permissions;
                        entry.is_subgraph = true;
                        libraries_.push_back(std::move(entry));
                    }
                }
            } catch (const std::exception& e) {
                // Log error, but don't interrupt loading
                // std::cerr << "[WARNING] Failed to load library from " << entry.path() << ": " << e.what() << std::endl;
                continue;
            }
        }
    }
}

} // namespace agenticdsl

```
    
## `parser/include/parser/markdown_parser.h`
    
```cpp
// modules/parser/include/parser/markdown_parser.h
#ifndef AGENTICDSL_MODULES_PARSER_MARKDOWN_PARSER_H
#define AGENTICDSL_MODULES_PARSER_MARKDOWN_PARSER_H

#include "common/types/node.h" // 引入 Node, NodePath, ParsedGraph
#include "common/types/budget.h" // 引入 ExecutionBudget
#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>

namespace agenticdsl {

// ParsedGraph 结构体定义（可能需要在 common/types 中定义，或在此模块中定义）
// 为了模块独立性，暂时在此定义，后续可考虑移到 common/types
struct ParsedGraph {
    std::vector<std::unique_ptr<Node>> nodes;
    NodePath path; // e.g., /main
    nlohmann::json metadata; // graph-level metadata
    std::optional<ExecutionBudget> budget; // 从 /__meta__ 解析
    std::optional<std::string> signature; // 子图签名
    std::vector<std::string> permissions; // 子图权限
    bool is_standard_library = false; // 路径以 /lib/ 开头
    std::optional<nlohmann::json> output_schema; // v3.1: 解析 signature.outputs 为 JSON Schema
    // ParsedGraph() = default;
    // ParsedGraph(const ParsedGraph&) = delete;            // 禁止拷贝
    // ParsedGraph& operator=(const ParsedGraph&) = delete; // 禁止拷贝赋值
    // ParsedGraph(ParsedGraph&&) = default;                // 允许移动
    // ParsedGraph& operator=(ParsedGraph&&) = default;     // 允许移动赋值
};

class MarkdownParser {
public:
    std::vector<ParsedGraph> parse_from_string(const std::string& markdown_content);
    std::vector<ParsedGraph> parse_from_file(const std::string& file_path);

    std::unique_ptr<Node> create_node_from_json(const NodePath& path, const nlohmann::json& node_json);

private:
    void validate_nodes(const std::vector<std::unique_ptr<Node>>& nodes);
    // Helper to parse signature.outputs into JSON Schema
    std::optional<nlohmann::json> parse_output_schema_from_signature(const std::string& signature_str);
};

} // namespace agenticdsl

#endif // AGENTICDSL_MODULES_PARSER_MARKDOWN_PARSER_H

```
    
## `parser/src/markdown_parser.cpp`
    
```cpp
// modules/parser/src/markdown_parser.cpp
#include "parser/markdown_parser.h"
#include "common/utils/parser_utils.h"
#include "common/utils/yaml_json.h"
#include "common/utils/template_renderer.h"
#include "common/types/node.h" 
#include "common/types/resource.h"
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
                    global_budget = budget;
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

```
    
## `system/include/system/system_nodes.h`
    
```cpp
// modules/system/include/system/system_nodes.h
#ifndef AGENTICDSL_MODULES_SYSTEM_SYSTEM_NODES_H
#define AGENTICDSL_MODULES_SYSTEM_SYSTEM_NODES_H

#include "common/types/node.h" // 引入 Node
#include <vector>
#include <memory> // for unique_ptr

namespace agenticdsl {

std::vector<std::unique_ptr<Node>> create_system_nodes();

} // namespace agenticdsl

#endif // AGENTICDSL_MODULES_SYSTEM_SYSTEM_NODES_H

```
    
## `system/src/system_nodes.cpp`
    
```cpp
// modules/system/src/system_nodes.cpp
#include "system/system_nodes.h"
#include "common/types/node.h" // Include Node definitions if not available via common/types/common.h

namespace agenticdsl {

std::vector<std::unique_ptr<Node>> create_system_nodes() {
    std::vector<std::unique_ptr<Node>> nodes;

    // /__system__/budget_exceeded → hard 终止
    auto budget_node = std::make_unique<EndNode>("/__system__/budget_exceeded");
    budget_node->metadata["termination_mode"] = "hard";
    nodes.push_back(std::move(budget_node));

    // /__system__/end_soft → soft 终止（供标准库使用）
    auto soft_end = std::make_unique<EndNode>("/__system__/end_soft");
    soft_end->metadata["termination_mode"] = "soft";
    nodes.push_back(std::move(soft_end));

    // /__system__/noop → 标准库 soft 终止单元（可选，推荐）
    auto noop = std::make_unique<EndNode>("/__system__/noop");
    noop->metadata["termination_mode"] = "soft";
    nodes.push_back(std::move(noop));

    return nodes;
}

} // namespace agenticdsl

```
    
## `executor/include/executor/node_executor.h`
    
```cpp
// modules/executor/include/executor/node_executor.h
#ifndef AGENTICDSL_MODULES_EXECUTOR_NODE_EXECUTOR_H
#define AGENTICDSL_MODULES_EXECUTOR_NODE_EXECUTOR_H

#include "common/types/context.h" // 引入 Context
#include "common/types/node.h"    // 引入 NodePath, Node, NodeType, StartNode, EndNode, etc.
#include "common/types/resource.h" // 引入 ResourceType
#include "common/utils/template_renderer.h" // 引入 InjaTemplateRenderer
#include "agenticdsl/tools/registry.h" // 引入 ToolRegistry
#include "agenticdsl/llm/llama_adapter.h" // 引入 LlamaAdapter
#include "agenticdsl/resources/manager.h" // 引入 ResourceManager
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace agenticdsl {

class NodeExecutor {
public:
    NodeExecutor(ToolRegistry& tool_registry, LlamaAdapter* llm_adapter = nullptr);

    // 执行一个节点，返回新的上下文
    Context execute_node(Node* node, const Context& ctx);
    void set_append_graphs_callback(AppendGraphsCallback cb) {
        append_graphs_callback_ = std::move(cb);
    }

private:
    ToolRegistry& tool_registry_;
    LlamaAdapter* llm_adapter_; // 可为 nullptr
    AppendGraphsCallback append_graphs_callback_;

    // 权限检查
    void check_permissions(const std::vector<std::string>& perms, const NodePath& node_path);

    // 内部执行方法，根据节点类型分发
    Context execute_start(const StartNode* node, const Context& ctx);
    Context execute_end(const EndNode* node, const Context& ctx);
    Context execute_assign(const AssignNode* node, const Context& ctx);
    Context execute_llm_call(const LLMCallNode* node, const Context& ctx);
    Context execute_tool_call(const ToolCallNode* node, const Context& ctx);
    Context execute_resource(const ResourceNode* node, const Context& ctx);
};

} // namespace agenticdsl

#endif // AGENTICDSL_MODULES_EXECUTOR_NODE_EXECUTOR_H

```
    
## `executor/src/node_executor.cpp`
    
```cpp
// modules/executor/src/node_executor.cpp
#include "executor/node_executor.h"
#include "agenticdsl/llm/prompt_builder.h" // 引入 PromptBuilder (用于注入库上下文)
#include "common/utils/template_renderer.h" // 引入 InjaTemplateRenderer (for rendering)
#include <stdexcept>
#include <inja/inja.hpp> // For RenderError
#include <algorithm> // For std::find
#include <thread> // For std::this_thread::sleep_for (if needed for mock)
#include <chrono> // For std::chrono_literals

namespace agenticdsl {

NodeExecutor::NodeExecutor(ToolRegistry& tool_registry, LlamaAdapter* llm_adapter)
    : tool_registry_(tool_registry), llm_adapter_(llm_adapter) {
    // llm_adapter_ 可能为 nullptr，NodeExecutor 需要处理这种情况
}

Context NodeExecutor::execute_node(Node* node, const Context& ctx) {
    // 注入资源上下文
    //Context context_with_resources = ctx;
    //auto resources_ctx = ResourceManager::instance().get_resources_context();
    //if (!resources_ctx.empty()) {
    //    context_with_resources["resources"] = resources_ctx;
    //}

    // 检查权限
    check_permissions(node->permissions, node->path);

    // 根据节点类型分发执行
    switch (node->type) {
        case NodeType::START:
            return execute_start(dynamic_cast<const StartNode*>(node), context_with_resources);
        case NodeType::END:
            return execute_end(dynamic_cast<const EndNode*>(node), context_with_resources);
        case NodeType::ASSIGN:
            return execute_assign(dynamic_cast<const AssignNode*>(node), context_with_resources);
        case NodeType::LLM_CALL:
            return execute_llm_call(dynamic_cast<const LLMCallNode*>(node), context_with_resources);
        case NodeType::TOOL_CALL:
            return execute_tool_call(dynamic_cast<const ToolCallNode*>(node), context_with_resources);
        case NodeType::RESOURCE:
            return execute_resource(dynamic_cast<const ResourceNode*>(node), context_with_resources);
        case NodeType::FORK:
            return execute_fork(dynamic_cast<const ForkNode*>(node), context_with_resources);
        case NodeType::JOIN:
            return execute_join(dynamic_cast<const JoinNode*>(node), context_with_resources);
        case NodeType::GENERATE_SUBGRAPH:
            return execute_generate_subgraph(dynamic_cast<const GenerateSubgraphNode*>(node), context_with_resources);
        case NodeType::ASSERT:
            return execute_assert(dynamic_cast<const AssertNode*>(node), context_with_resources);
        default:
            throw std::runtime_error("Unknown node type during execution: " + std::to_string(static_cast<int>(node->type)));
    }
}

void NodeExecutor::check_permissions(const std::vector<std::string>& perms, const NodePath& node_path) {
    // 简单实现：检查是否有权限要求，如果有，假设都需要（实际应用中需更细粒度）
    for (const auto& perm : perms) {
        // Example: "tool: web_search"
        if (perm.substr(0, 5) == "tool:") {
            std::string tool_name = perm.substr(6); // Extract name after "tool: "
            if (!tool_registry_.has_tool(tool_name)) {
                throw std::runtime_error("Permission denied: Tool '" + tool_name + "' not available for node: " + node_path);
            }
        }
        // Add more permission checks as needed (network, file, etc.)
    }
    // 例如，检查 perms 中是否包含 "network" 或 "tool: web_search" 等
    // 这里只是示例，实际权限检查逻辑会更复杂

Context NodeExecutor::execute_start(const StartNode* node, const Context& ctx) {
    // Start 节点通常不修改上下文，直接返回
    return ctx;
}

Context NodeExecutor::execute_end(const EndNode* node, const Context& ctx) {
    // End 节点通常不修改上下文，直接返回
    // 其终止逻辑由 TopoScheduler 处理
    return ctx;
}

Context NodeExecutor::execute_assign(const AssignNode* node, const Context& ctx) {
    Context new_context = ctx;
    for (const auto& [key, template_str] : node->assign) {
        try {
            std::string rendered_value = InjaTemplateRenderer::render(template_str, ctx);
            new_context[key] = rendered_value; // 赋值到新的上下文
        } catch (const inja::RenderError& e) {
            throw std::runtime_error("Template rendering failed for key '" + key + "': " + std::string(e.what()));
        }
    }
    return new_context;
}

Context NodeExecutor::execute_llm_call(const LLMCallNode* node, const Context& ctx) {
    if (!llm_adapter_) {
        throw std::runtime_error("LLM adapter not available for node: " + node->path);
    }

    Context new_context = ctx;
    try {
        // 使用 PromptBuilder 注入库信息
        std::string rendered_prompt = PromptBuilder::inject_libraries_into_prompt(node->prompt_template, ctx);

        std::string llm_response = llm_adapter_->generate(rendered_prompt);

        // 将 LLM 响应赋值到上下文
        if (!node->output_keys.empty()) {
            new_context[node->output_keys[0]] = llm_response;
        } else {
            // 如果没有 output_keys，可能需要特殊处理，或者规范应强制要求
            // 此处假设至少有一个 output_key
            throw std::runtime_error("LLMCallNode has no output_keys: " + node->path);
        }
    } catch (const inja::RenderError& e) {
        throw std::runtime_error("Prompt template rendering failed for node '" + node->path + "': " + std::string(e.what()));
    }
    return new_context;
}

Context NodeExecutor::execute_tool_call(const ToolCallNode* node, const Context& ctx) {
    Context new_context = ctx;

    // 渲染参数
    std::unordered_map<std::string, std::string> rendered_args;
    for (const auto& [key, tmpl] : node->arguments) {
        rendered_args[key] = InjaTemplateRenderer::render(tmpl, ctx);
    }

    // 调用工具
    if (!tool_registry_.has_tool(node->tool_name)) {
        throw std::runtime_error("Tool '" + node->tool_name + "' not registered for node: " + node->path);
    }

    nlohmann::json result = tool_registry_.call_tool(node->tool_name, rendered_args);

    // 处理 output_keys
    if (node->output_keys.size() == 1) {
        new_context[node->output_keys[0]] = result;
    } else if (result.is_object()) {
        for (const auto& key : node->output_keys) {
            if (result.contains(key)) {
                new_context[key] = result[key];
            }
        }
    } else {
        // 如果 result 不是对象，或者 output_keys 多于 1 个，按第一个 key 赋值
        if (!node->output_keys.empty()) {
            new_context[node->output_keys[0]] = result;
        }
    }

    return new_context;
}

Context NodeExecutor::execute_resource(const ResourceNode* node, const Context& ctx) {
    /*
    // 创建 Resource 对象并注册
    Resource resource;
    resource.path = node->path;
    resource.resource_type = node->resource_type;
    resource.uri = node->uri;
    resource.scope = node->scope;
    resource.metadata = node->metadata; // Use node's metadata

    ResourceManager::instance().register_resource(resource);
        */

    // Resource 节点通常不修改上下文，直接返回
    return ctx;
}

Context NodeExecutor::execute_assert(const AssertNode* node, const Context& ctx) {
    // Render the condition expression using the current context
    std::string rendered_condition_str;
    try {
        rendered_condition_str = InjaTemplateRenderer::render(node->condition, ctx);
    } catch (const inja::InjaError& e) {
        throw std::runtime_error("Assert condition template rendering failed for node '" + node->path + "': " + std::string(e.message));
    }

    // Convert rendered result to boolean
    // Inja renders to string, so we check string value
    bool condition_result = false;
    if (rendered_condition_str == "true") {
        condition_result = true;
    } else if (rendered_condition_str == "false") {
        condition_result = false;
    } else {
        // If the rendered result is not explicitly "true" or "false",
        // try to interpret it as a number (0 is false, non-zero is true)
        try {
            double num_val = std::stod(rendered_condition_str);
            condition_result = (num_val != 0.0);
    } catch (...) {
            // If it's not a number either, treat as false or throw error
            // Let's throw an error for non-boolean results
            throw std::runtime_error("Assert condition did not evaluate to a boolean value ('true'/'false' or number): " + rendered_condition_str);
        }
    }

    if (!condition_result) {
        // Condition failed
        if (node->on_failure.has_value()) {
            // This requires the scheduler to handle jumps.
            // For now, throw an error indicating the jump path.
            // The scheduler will catch this and handle the jump.
            throw std::runtime_error("Assert failed. Jumping to: " + node->on_failure.value());
        } else {
            // No jump path, just fail the execution
            throw std::runtime_error("Assert failed at node: " + node->path);
        }
    }
    // Condition passed, context remains unchanged
    return ctx;
}

Context NodeExecutor::execute_fork(const ForkNode* node, const Context& ctx) {
    // ForkNode 的执行逻辑需要由 TopoScheduler 处理，因为它涉及并发和上下文管理。
    // NodeExecutor 本身不能启动新的线程或任务。
    // 因此，这里抛出异常，提示需要在调度器层面实现。
    throw std::runtime_error("ForkNode execution requires concurrent scheduler support, not implemented in NodeExecutor.");
    // In a concurrent scheduler:
    // 1. Create context copies for each branch.
    // 2. Schedule each branch path to run concurrently.
    // 3. Wait for all branches to complete (JoinNode would handle waiting).
    // 4. Trigger snapshot here according to v3.1 spec.
    return ctx; // Placeholder
}

Context NodeExecutor::execute_join(const JoinNode* node, const Context& ctx) {
    // JoinNode 的执行逻辑也需要由 TopoScheduler 处理，因为它需要等待其他分支完成。
    // NodeExecutor 本身无法等待。
    // 因此，这里抛出异常，提示需要在调度器层面实现。
    throw std::runtime_error("JoinNode execution requires concurrent scheduler support, not implemented in NodeExecutor.");
    // In a concurrent scheduler:
    // 1. Wait for all nodes in 'wait_for' to finish.
    // 2. Retrieve their final contexts.
    // 3. Merge them into the current context using 'merge_strategy'.
    // 4. Return the merged context.
    return ctx; // Placeholder
}

Context NodeExecutor::execute_generate_subgraph(const GenerateSubgraphNode* node, const Context& ctx) {
    Context new_context = ctx;
    try {
        // 1. Inject available_subgraphs and budget info into prompt
        std::string rendered_prompt = PromptBuilder::inject_libraries_into_prompt(node->prompt_template, ctx);
        // Add budget info to prompt context if needed by LLM
        Context prompt_ctx = ctx;
        prompt_ctx["available_subgraphs"] = PromptBuilder::build_available_libraries_context();
         // Add budget info (nodes_left, depth_left, etc.) - This requires access to ExecutionSession's budget
         // For now, assume budget info is added by the calling context or PromptBuilder
         // prompt_ctx["budget"] = ...; // Access budget from ExecutionSession

        // 2. Call LLM
        std::string generated_dsl;
        if (llm_adapter_) {
            generated_dsl = llm_adapter_->generate(rendered_prompt);
        } else {
            throw std::runtime_error("LLM adapter not available for generate_subgraph");
        }

        // 3. Parse LLM output for `### AgenticDSL '/dynamic/...'` blocks
        // This requires access to the main engine's parser and graph storage mechanism.
        // A reference or callback to the engine might be needed here, or the parsing result is returned.
        // For this executor, assume a global or injected mechanism or return the result for the scheduler to handle.
        // Let's assume the scheduler calls a parser and handles the dynamic graph registration.
        // Here, we just parse and return the generated paths in the context.
        auto new_graphs = MarkdownParser::parse_from_string(generated_dsl); // Reuse parser
        std::vector<std::string> dynamic_paths; // Collect paths of generated graphs
        for (auto& graph : new_graphs) {
            if (graph.path.rfind("/dynamic/", 0) == 0) { // Ensure it's dynamic
                // 4. Validate signature if present (v3.1)
                if (graph.signature.has_value()) {
                    // Perform validation based on signature_validation policy
                    bool is_valid = true; // Placeholder for actual validation logic
                    if (!is_valid && node->signature_validation == "strict") {
                         if (node->on_signature_violation.has_value()) {
                             // Trigger jump to on_signature_violation path
                             // This requires scheduler logic to handle jumps
                             // For now, throw error
                             throw std::runtime_error("Signature validation failed (strict mode) for generated graph: " + graph.path);
                         } else {
                             // Default behavior for strict violation without jump path
                             throw std::runtime_error("Signature validation failed (strict mode) for generated graph: " + graph.path);
                         }
                    } else if (!is_valid && node->signature_validation == "warn") {
                        // Log warning but continue
                        std::cerr << "[WARNING] Signature validation failed (warn mode) for generated graph: " << graph.path << std::endl;
                    } // ignore: do nothing
                }
                dynamic_paths.push_back(graph.path);
                // 5. Register new graph (This logic belongs in the scheduler/engine)
                // g_current_engine->append_graphs({std::move(graph)}); // Placeholder - requires access to engine/scheduler
            }
        }

        // 6. Store generated graph path(s) in context
        if (!node->output_keys.empty()) {
            if (dynamic_paths.size() == 1) {
                new_context[node->output_keys[0]] = dynamic_paths[0];
            } else {
                new_context[node->output_keys[0]] = dynamic_paths; // Store as array if multiple
            }
        }

        // 7. Snapshot trigger happens in ExecutionSession::execute_node, not here.

    } catch (const std::exception & e) {
        throw std::runtime_error("GenerateSubgraphNode execution failed: " + std::string(e.what()));
    }
    return new_context;
}

} // namespace agenticdsl

```
    
## `budget/include/budget/budget_controller.h`
    
```cpp
// modules/budget/include/budget/budget_controller.h
#ifndef AGENTICDSL_MODULES_BUDGET_BUDGET_CONTROLLER_H
#define AGENTICDSL_MODULES_BUDGET_BUDGET_CONTROLLER_H

#include "common/types/node.h" // 引入 NodePath
#include "common/types/budget.h" // 引入 ExecutionBudget (已包含 atomic 计数器)
#include <optional>
#include <chrono> // For steady_clock used in ExecutionBudget
#include <mutex>  // Potentially needed if ExecutionBudget needs external protection beyond atomics

namespace agenticdsl {

// BudgetController 类封装了预算的管理和检查逻辑
class BudgetController {
public:
    // 构造函数，接受一个可选的初始预算配置
    explicit BudgetController(std::optional<ExecutionBudget> initial_budget = std::nullopt);

    // 尝试消耗一个节点预算
    // 返回 true 表示消耗成功，false 表示超出预算或预算为无限
    bool try_consume_node();

    // 尝试消耗一个 LLM 调用预算
    // 返回 true 表示消耗成功，false 表示超出预算或预算为无限
    bool try_consume_llm_call();

    // 检查当前预算是否已超限
    bool exceeded() const;

    // 获取预算超限时的跳转目标节点路径
    // 返回 std::nullopt 如果没有超限或没有配置跳转路径
    void set_termination_target(const NodePath& target) { termination_target_ = target; }
    std::optional<NodePath> get_termination_target() const;

    // 获取当前预算配置的副本
    std::optional<ExecutionBudget> get_budget() const;

    // 设置预算配置
    void set_budget(const std::optional<ExecutionBudget>& budget);

private:
    std::optional<ExecutionBudget> budget_opt_;
    NodePath termination_target_ = "/__system__/budget_exceeded"; // 默认超限跳转路径
};

} // namespace agenticdsl

#endif // AGENTICDSL_MODULES_BUDGET_BUDGET_CONTROLLER_H

```
    
## `budget/src/budget_controller.cpp`
    
```cpp
// modules/budget/src/budget_controller.cpp
#include "budget/budget_controller.h"
#include <chrono>
#include <iostream> // For debugging if needed

namespace agenticdsl {

BudgetController::BudgetController(std::optional<ExecutionBudget> initial_budget)
    : budget_opt_(std::move(initial_budget)) {
    // 如果提供了初始预算，初始化其 start_time
    if (budget_opt_.has_value()) {
        budget_opt_->start_time = std::chrono::steady_clock::now();
    }
}

bool BudgetController::try_consume_node() {
    if (!budget_opt_.has_value()) {
        // 没有预算限制，总是成功
        return true;
    }

    return budget_opt_->try_consume_node(); // ExecutionBudget 内部处理原子性
}

bool BudgetController::try_consume_llm_call() {
    if (!budget_opt_.has_value()) {
        // 没有预算限制，总是成功
        return true;
    }

    return budget_opt_->try_consume_llm_call(); // ExecutionBudget 内部处理原子性
}

bool BudgetController::exceeded() const {
    if (!budget_opt_.has_value()) {
        // 没有预算限制，永不超限
        return false;
    }

    return budget_opt_->exceeded(); // ExecutionBudget 内部检查原子计数器和时间
}

std::optional<NodePath> BudgetController::get_termination_target() const {
    if (exceeded()) {
        return termination_target_;
    }
    return std::nullopt;
}

std::optional<ExecutionBudget> BudgetController::get_budget() const {
    return budget_opt_;
}

void BudgetController::set_budget(const std::optional<ExecutionBudget>& budget) {
    budget_opt_ = budget;
    if (budget_opt_.has_value()) {
        // 重置开始时间
        budget_opt_->start_time = std::chrono::steady_clock::now();
    }
}

} // namespace agenticdsl

```
    