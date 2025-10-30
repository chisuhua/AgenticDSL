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
