#ifndef AGENFLOW_EXECUTOR_H
#define AGENFLOW_EXECUTOR_H

#include "nodes.h"
#include "common/types.h"
#include <vector>
#include <unordered_map>
#include <memory>

namespace agenticdsl {

class ModernFlowExecutor {
public:
    ModernFlowExecutor(std::vector<std::unique_ptr<Node>> nodes);
    ExecutionResult execute(const Context& initial_context = Context{});

private:
    std::vector<std::unique_ptr<Node>> nodes_;
    std::unordered_map<NodePath, Node*> node_map_;

    Node* find_start_node() const;
    Node* get_node(const NodePath& path) const;

    static const size_t MAX_STEPS = 100; // 防止无限循环
};

} // namespace agenticdsl

#endif
