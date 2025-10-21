#include "agenticdsl/core/executor.h"
#include "agenticdsl/resources/manager.h" // v1.1: New include
#include <stdexcept>
#include <iostream>

namespace agenticdsl {

ModernFlowExecutor::ModernFlowExecutor(std::vector<std::unique_ptr<Node>> nodes)
    : nodes_(std::move(nodes)) {
    // 构建节点映射表
    for (const auto& node : nodes_) {
        node_map_[node->path] = node.get();
    }
}

ExecutionResult ModernFlowExecutor::execute(const Context& initial_context) {
    // v1.1: Inject resources into the initial context
    Context context = initial_context;
    auto resources_ctx = ResourceManager::instance().get_resources_context();
    if (!resources_ctx.empty()) {
        context["resources"] = resources_ctx; // Add resources as a read-only namespace
    }

    // 找到起始节点
    Node* current_node = find_start_node();
    if (!current_node) {
        return {false, "No start node found", context};
    }

    size_t step_count = 0;

    while (current_node && step_count < MAX_STEPS) {
        try {
            context = current_node->execute(context);
        } catch (const std::exception& e) {
            return {false, "Node execution failed: " + std::string(e.what()), context};
        }

        // 如果是End节点，退出
        if (current_node->type == NodeType::END) {
            break;
        }

        // 获取下一个节点 (v1.1: Now supports multiple next paths, pick first for v1 linear execution)
        if (current_node->next.empty()) {
            break; // 没有下一个节点
        }

        // v1: Simple strategy - take the first next path
        NodePath next_path = current_node->next[0];
        auto next_it = node_map_.find(next_path);
        if (next_it == node_map_.end()) {
            return {false, "Next node '" + next_path + "' not found", context};
        }

        current_node = next_it->second;
        ++step_count;
    }

    if (step_count >= MAX_STEPS) {
        return {false, "Maximum execution steps exceeded", context};
    }

    return {true, "Execution completed successfully", context};
}

Node* ModernFlowExecutor::find_start_node() const {
    for (const auto& node : nodes_) {
        if (node->type == NodeType::START) {
            return node.get();
        }
    }
    return nullptr;
}

Node* ModernFlowExecutor::get_node(const NodePath& path) const {
    auto it = node_map_.find(path);
    return (it != node_map_.end()) ? it->second : nullptr;
}

} // namespace agenticdsl
