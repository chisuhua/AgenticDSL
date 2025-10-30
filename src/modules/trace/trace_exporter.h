// modules/trace/include/trace/trace_exporter.h
#ifndef AGENTICDSL_MODULES_TRACE_TRACE_EXPORTER_H
#define AGENTICDSL_MODULES_TRACE_TRACE_EXPORTER_H

#include "core/types/node.h"    // 引入 NodePath
#include "core/types/context.h" // 引入 Context
#include "core/types/budget.h"  // 引入 ExecutionBudget
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
