#include "agenticdsl/core/executor.h"
#include "agenticdsl/resources/manager.h"
#include <stdexcept>

namespace agenticdsl {

DAGFlowExecutor::DAGFlowExecutor(std::vector<ParsedGraph> graph)
    for (auto& graph : graphs) {
        if (graph.path == "/main") {
            // Subgraph: nodes have local id, path = /main/{id}
            for (auto& node : graph.nodes) {
                node_map_[node->path] = node.get();
                all_nodes_.push_back(std::move(node));
            }
        } else {
            // Single node block (e.g., /main/step1)
            for (auto& node : graph.nodes) {
                node_map_[node->path] = node.get();
                all_nodes_.push_back(std::move(node));
            }
        }
    }

    current_path_ = "/main/start";
}

ExecutionResult DAGFlowExecutor::execute(const Context& initial_context) {
    Context context = initial_context;

    auto resources_ctx = ResourceManager::instance().get_resources_context();
    if (!resources_ctx.empty()) {
        context["resources"] = resources_ctx;
    }

    while (!current_path_.empty()) {
        auto it = node_map_.find(current_path_);
        if (it == node_map_.end()) {
            return {false, "Node not found: " + current_path_, context, std::nullopt};
        }

        Node* node = it->second;

        // Skip already executed nodes
        if (executed_nodes_.count(current_path_)) {
            if (!node->next.empty()) {
                current_path_ = node->next[0];
                continue;
            } else {
                break;
            }
        }

        // Execute node
        try {
            context = node->execute(context);
            executed_nodes_.insert(current_path_);
        } catch (const std::exception& e) {
            return {false, "Execution failed at " + current_path_ + ": " + std::string(e.what()), context, std::nullopt};
        }

        // Pause if llm_call
        if (node->type == NodeType::LLM_CALL) {
            return {true, "Paused at LLM call", context, current_path_};
        }

        // End node
        if (node->type == NodeType::END) {
            break;
        }

        // Move to next
        if (!node->next.empty()) {
            current_path_ = node->next[0];
        } else {
            break;
        }
    }

    return {true, "Completed", context, std::nullopt};
}

} // namespace agenticdsl
