#ifndef AGENTICDSL_TYPES_BUDGET_H
#define AGENTICDSL_TYPES_BUDGET_H

#include "context.h" // 引入 Context/Value
#include <chrono>
#include <optional>
#include <atomic> // For atomic budget counters (v3.1 requirement)

namespace agenticdsl {

using NodePath = std::string; // e.g., "/main/step1"

// 执行预算结构
struct ExecutionBudget {
    int max_nodes = -1;           // -1 表示无限制
    int max_llm_calls = -1;
    int max_duration_sec = -1;
    int max_subgraph_depth = -1;
    int max_snapshots = -1;           // -1 表示无限制
    size_t snapshot_max_size_kb = 512;



    // Use atomic integers for thread-safe budget updates
    mutable std::atomic<int> nodes_used{0};
    mutable std::atomic<int> llm_calls_used{0};
    mutable std::atomic<int> subgraph_depth_used{0};
    std::chrono::steady_clock::time_point start_time;

    // Constructor to initialize start_time
    ExecutionBudget() : start_time(std::chrono::steady_clock::now()) {}

    // 显式定义移动构造函数（重置原子计数器）
    ExecutionBudget(ExecutionBudget&& other) noexcept
        : max_nodes(other.max_nodes),
          max_llm_calls(other.max_llm_calls),
          max_duration_sec(other.max_duration_sec),
          max_subgraph_depth(other.max_subgraph_depth),
          max_snapshots(other.max_snapshots),
          snapshot_max_size_kb(other.snapshot_max_size_kb),
          nodes_used(0), // 重置计数器！
          llm_calls_used(0),
          subgraph_depth_used(0),
          start_time(std::chrono::steady_clock::now()) // 重置开始时间
    {}

    // 显式定义移动赋值运算符
    ExecutionBudget& operator=(ExecutionBudget&& other) noexcept {
        if (this != &other) {
            max_nodes = other.max_nodes;
            max_llm_calls = other.max_llm_calls;
            max_duration_sec = other.max_duration_sec;
            max_subgraph_depth = other.max_subgraph_depth;
            max_snapshots = other.max_snapshots;
            snapshot_max_size_kb = other.snapshot_max_size_kb;
            // 重置原子计数器（不移动 other 的值）
            nodes_used = 0;
            llm_calls_used = 0;
            subgraph_depth_used = 0;
            start_time = std::chrono::steady_clock::now();
        }
        return *this;
    }

    // 禁止拷贝（可选但推荐）
    ExecutionBudget(const ExecutionBudget&) = delete;
    ExecutionBudget& operator=(const ExecutionBudget&) = delete;

    // Check if budget is exceeded (const, does not modify state directly, but reads atomics)
    bool exceeded() const {
        if (max_nodes >= 0 && nodes_used.load() > max_nodes) return true;
        if (max_llm_calls >= 0 && llm_calls_used.load() > max_llm_calls) return true;
        if (max_subgraph_depth >= 0 && subgraph_depth_used.load() > max_subgraph_depth) return true;
        if (max_duration_sec >= 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time).count();
            if (elapsed > max_duration_sec) return true;
        }
        return false;
    }

    // Helper methods to atomically consume budget
    bool try_consume_node() {
        int current = nodes_used.load();
        if (max_nodes >= 0 && current >= max_nodes) return false; // Would exceed
        int expected = current;
        while (!nodes_used.compare_exchange_weak(expected, expected + 1)) {
            if (max_nodes >= 0 && expected >= max_nodes) return false; // Another thread consumed the last allowed node
        }
        return true;
    }

    bool try_consume_llm_call() {
        int current = llm_calls_used.load();
        if (max_llm_calls >= 0 && current >= max_llm_calls) return false; // Would exceed
        int expected = current;
        while (!llm_calls_used.compare_exchange_weak(expected, expected + 1)) {
            if (max_llm_calls >= 0 && expected >= max_llm_calls) return false; // Another thread consumed the last allowed call
        }
        return true;
    }
    // v3.1 新增：子图深度消耗
    bool try_consume_subgraph_depth() {
        int current = subgraph_depth_used.load();
        if (max_subgraph_depth >= 0 && current >= max_subgraph_depth) return false;
        int expected = current;
        while (!subgraph_depth_used.compare_exchange_weak(expected, expected + 1)) {
            if (max_subgraph_depth >= 0 && expected >= max_subgraph_depth) return false;
        }
        return true;
    }

};

// ExecutionResult structure
struct ExecutionResult {
    bool success;
    std::string message; // 错误信息或成功信息
    Context final_context; // 执行结束时的上下文
    std::optional<NodePath> paused_at; // set if paused at llm_call
};

} // namespace agenticdsl::types

#endif // AGENTICDSL_TYPES_BUDGET_H
