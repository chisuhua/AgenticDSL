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
