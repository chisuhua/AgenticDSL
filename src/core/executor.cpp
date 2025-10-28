// src/core/executor.cpp
#include "agenticdsl/core/executor.h"
#include "agenticdsl/core/system_nodes.h"
#include "agenticdsl/resources/manager.h"
#include <stdexcept>

namespace agenticdsl {

static std::optional<ExecutionBudget> extract_budget(const std::vector<ParsedGraph>& graphs) {
    for (const auto& g : graphs) {
        if (g.budget.has_value()) {
            return g.budget;
        }
    }
    return std::nullopt;
}

DAGExecutor::DAGExecutor(const std::vector<ParsedGraph>& graphs) {
    auto budget = extract_budget(graphs);
    TopoScheduler::Config config;
    config.initial_budget = budget; 
    scheduler_ = TopoScheduler(config);
    load_graphs(graphs);
}

void DAGExecutor::load_graphs(const std::vector<ParsedGraph>& graphs) {
    // 1. 注册系统节点
    auto sys_nodes = create_system_nodes();
    for (auto& node : sys_nodes) {
        scheduler_.register_node(std::move(node));
    }

    // 2. 注册用户节点
    for (const auto& graph : graphs) {
        for (const auto& node : graph.nodes) {
            if (node) {
                scheduler_.register_node(node->clone());
            }
        }
    }

    // 3. 构建 DAG
    scheduler_.build_dag();
}

ExecutionResult DAGExecutor::execute(const Context& initial_context) {
    Context context = initial_context;
    return scheduler_.execute(std::move(context));
}

} // namespace agenticdsl
