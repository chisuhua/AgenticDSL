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

// modules/budget/src/budget_controller.cpp
bool BudgetController::try_consume_subgraph_depth() {
    if (!budget_opt_.has_value()) {
        return true; // 无限制
    }
    return budget_opt_->try_consume_subgraph_depth(); // ExecutionBudget 需实现此方法
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

const std::optional<ExecutionBudget>& BudgetController::get_budget() const {
    return budget_opt_;
}

void BudgetController::set_budget(std::optional<ExecutionBudget> budget) {
    budget_opt_ = std::move(budget);
    if (budget_opt_.has_value()) {
        // 重置开始时间
        budget_opt_->start_time = std::chrono::steady_clock::now();
    }
}

} // namespace agenticdsl
