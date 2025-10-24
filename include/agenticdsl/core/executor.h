// agenticdsl/core/executor.h
#ifndef AGENTICDSL_CORE_EXECUTOR_H
#define AGENTICDSL_CORE_EXECUTOR_H

#include "agenticdsl/scheduler/topo_scheduler.h"
#include "agenticdsl/core/parser.h"
#include "common/types.h"
#include <vector>
#include <memory>

namespace agenticdsl {

/**
 * DAGExecutor: 基于 TopoScheduler 的 DAG 执行器（替代原 DAGFlowExecutor）
 */
class DAGExecutor {
public:
    explicit DAGExecutor(const std::vector<ParsedGraph>& main_graph);
    ExecutionResult execute(const Context& initial_context = Context{});

private:
    TopoScheduler scheduler_;
    void load_graphs(const std::vector<ParsedGraph>& graphs);
};

} // namespace agenticdsl

#endif
