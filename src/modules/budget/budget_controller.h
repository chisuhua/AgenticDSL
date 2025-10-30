// modules/budget/include/budget/budget_controller.h
#ifndef AGENTICDSL_MODULES_BUDGET_BUDGET_CONTROLLER_H
#define AGENTICDSL_MODULES_BUDGET_BUDGET_CONTROLLER_H

#include "core/types/budget.h" // 引入 ExecutionBudget (已包含 atomic 计数器)
#include "core/types/node.h" // 引入 NodePath
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

    bool try_consume_subgraph_depth();

    // 检查当前预算是否已超限
    bool exceeded() const;

    // 获取预算超限时的跳转目标节点路径
    // 返回 std::nullopt 如果没有超限或没有配置跳转路径
    void set_termination_target(const NodePath& target) { termination_target_ = target; }
    std::optional<NodePath> get_termination_target() const;



    // 获取当前预算配置的副本
    const std::optional<ExecutionBudget>& get_budget() const;

    // 设置预算配置
    void set_budget(std::optional<ExecutionBudget> budget);

private:
    std::optional<ExecutionBudget> budget_opt_;
    NodePath termination_target_ = "/__system__/budget_exceeded"; // 默认超限跳转路径
};

} // namespace agenticdsl

#endif // AGENTICDSL_MODULES_BUDGET_BUDGET_CONTROLLER_H
