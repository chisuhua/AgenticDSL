// agenticdsl/scheduler/topo_scheduler.h
#ifndef AGENTICDSL_SCHEDULER_TOPO_SCHEDULER_H
#define AGENTICDSL_SCHEDULER_TOPO_SCHEDULER_H

#include "agenticdsl/core/nodes.h"
#include "common/types.h"
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <vector>
#include <functional>
#include <optional>

namespace agenticdsl {

class TopoScheduler {
public:
    struct Config {
        bool enable_soft_termination;
        std::optional<ExecutionBudget> initial_budget; // ← 新增：初始预算
        Config() : enable_soft_termination(true) {}
    };

    explicit TopoScheduler(Config config = {});

    // 注册节点（由 executor 调用）
    void register_node(std::unique_ptr<Node> node);

    // 构建 DAG（解析所有 next，建立反向边）
    void build_dag();

    // 执行整个 DAG，返回最终上下文
    ExecutionResult execute(Context initial_context);

    // 获取已执行节点（用于 LLM prompt 注入）
    const std::unordered_set<NodePath>& executed_nodes() const { return executed_; }

private:
    // 内部状态
    std::vector<std::unique_ptr<Node>> all_nodes_;
    std::unordered_map<NodePath, Node*> node_map_;
    std::unordered_map<NodePath, std::vector<NodePath>> reverse_edges_; // 后继 → 前驱
    std::unordered_map<NodePath, int> in_degree_;
    std::queue<NodePath> ready_queue_;
    std::unordered_set<NodePath> executed_;
    std::vector<NodePath> call_stack_; // 用于 soft 终止
    Config config_;

    // 当前执行预算（可为空，表示无限制）
    std::optional<ExecutionBudget> current_budget_;

    // 解析 wait_for 表达式（支持 any_of / all_of / 动态）
    std::vector<NodePath> parse_wait_for(const nlohmann::json& wait_for_expr, const Context& ctx);

    // 执行单个节点
    Context execute_node(Node* node, Context& context);

    // 处理 soft 终止：返回父图下一个节点
    NodePath handle_soft_end(const NodePath& current_path);

    // 检查预算是否超限，若超限则触发跳转
    bool check_and_enforce_budget(Node* node);
};

} // namespace agenticdsl

#endif
