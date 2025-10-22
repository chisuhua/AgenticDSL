#ifndef AGENTICDSL_CORE_EXECUTOR_H
#define AGENTICDSL_CORE_EXECUTOR_H

#include "agenticdsl/core/nodes.h"
#include "agenticdsl/core/parser.h"
#include "common/types.h"
#include <vector>
#include <unordered_map>
#include <memory>

namespace agenticdsl {

class DAGFlowExecutor {
public:
    explicit DAGFlowExecutor(std::vector<ParsedGraph> main_graph);
    ExecutionResult execute(const Context& initial_context = Context{});

private:
    std::vector<std::unique_ptr<Node>> all_nodes_;
    std::unordered_map<NodePath, Node*> node_map_;
    std::unordered_set<NodePath> executed_nodes_;
    NodePath current_path_;
    Node* find_start_node() const;
};

} // namespace agenticdsl

#endif
