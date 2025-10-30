// modules/context/include/context/context_engine.h
#ifndef AGENTICDSL_MODULES_CONTEXT_CONTEXT_ENGINE_H
#define AGENTICDSL_MODULES_CONTEXT_CONTEXT_ENGINE_H

#include "core/types/context.h" // 引入 Context, Value
#include "core/types/node.h"    // 引入 NodePath
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
