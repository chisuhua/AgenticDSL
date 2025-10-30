// modules/scheduler/src/execution_session.cpp
#include "scheduler/execution_session.h"
#include "common/utils/template_renderer.h" // 引入 InjaTemplateRenderer (for Trace context delta)
//#include "agenticdsl/llm/prompt_builder.h" // 引入 PromptBuilder
#include <stdexcept>
#include <algorithm> // For std::find

namespace agenticdsl {

ExecutionSession::ExecutionSession(
    std::optional<ExecutionBudget> initial_budget,
    ToolRegistry& tool_registry,
    LlamaAdapter* llm_adapter,
    ResourceManager& resource_manager, // ← 新增
    const std::vector<ParsedGraph>* full_graphs,
    AppendGraphsCallback append_graphs_callback)
    : budget_controller_(std::move(initial_budget)),
      node_executor_(tool_registry, llm_adapter),
      resource_manager_(resource_manager), // ← 初始化
      full_graphs_(full_graphs),
      append_graphs_callback_(std::move(append_graphs_callback)) { // Store callback

    node_executor_.set_append_graphs_callback(append_graphs_callback_);
    if (initial_budget.has_value()) {
        size_t max_snapshots = (initial_budget->max_snapshots >= 0) 
            ? static_cast<size_t>(initial_budget->max_snapshots) : 10;
        context_engine_.set_snapshot_limits(max_snapshots, initial_budget->snapshot_max_size_kb);
    } else {
        context_engine_.set_snapshot_limits(10, 512); // dev default
    }
}
nlohmann::json ExecutionSession::build_available_subgraphs_context() const {
    nlohmann::json libs = nlohmann::json::array();
    
    // 1. 静态标准库（/lib/**）
    auto& loader = StandardLibraryLoader::instance();
    for (const auto& entry : loader.get_available_libraries()) {
        if (entry.is_subgraph) {
            nlohmann::json lib;
            lib["path"] = entry.path;
            if (entry.output_schema && !entry.output_schema->is_null()) {
                lib["signature"] = {
                    {"outputs", entry.output_schema}
                };
            }
            lib["permissions"] = entry.permissions;
            lib["stability"] = "stable";
            libs.push_back(std::move(lib));
        }
    }
    
    // 2. 动态生成子图（/dynamic/**）
    if (full_graphs_) {
        for (const auto& graph : *full_graphs_) {
            if (graph.path.rfind("/dynamic/", 0) == 0 && !graph.output_schema->is_null()) {
                nlohmann::json lib;
                lib["path"] = graph.path;
                lib["signature"] = {
                    {"outputs", graph.output_schema}
                };
                lib["permissions"] = graph.permissions;
                lib["stability"] = "dynamic";
                libs.push_back(std::move(lib));
            }
        }
    }
    
    return libs;
}

std::string ExecutionSession::inject_subgraphs_into_prompt(
    const std::string& base_prompt,
    const Context& context) const {
    Context ctx = context;
    ctx["available_subgraphs"] = build_available_subgraphs_context();
    return InjaTemplateRenderer::render(base_prompt, ctx);
}


ExecutionSession::ExecutionResult ExecutionSession::execute_node(Node* node, const Context& initial_context) {
    ExecutionResult result;
    result.success = true;
    result.message = "Node executed successfully";

    Context context_with_resources = initial_context;
    auto resources_ctx = resource_manager_.get_resources_context(); // ← 需要 ExecutionSession 持有 resource_manager_
    if (!resources_ctx.empty()) {
        context_with_resources["resources"] = std::move(resources_ctx);
    }

    // v3.1: Check for snapshot trigger BEFORE execution
    bool snapshot_needed = needs_snapshot(node);
    if (snapshot_needed) {
        context_engine_.save_snapshot(node->path, context_with_resources); // Snapshot *before* execution
        result.snapshot_key = node->path;
    }

    // 1. 检查预算
    if (node->type == NodeType::LLM_CALL) {
        if (!budget_controller_.try_consume_llm_call()) {
            result.success = false;
            result.message = "Budget exceeded: LLM call limit reached";
            return result;
        }
    }
    if (node->type == NodeType::GENERATE_SUBGRAPH) {
        if (!budget_controller_.try_consume_subgraph_depth()) { // Assume BudgetController has this method
            result.success = false;
            result.message = "Budget exceeded: Subgraph depth limit reached";
            return result;
        }
    }
    if (!budget_controller_.try_consume_node()) {
        result.success = false;
        result.message = "Budget exceeded: Node limit reached";
        return result;
    }

    // 2. 记录 Trace 开始
    nlohmann::json initial_ctx_json = context_with_resources;
    trace_exporter_.on_node_start(node->path, node->type, initial_ctx_json, budget_controller_.get_budget());

    // 3. 执行节点
    try {
        // 统一执行路径
        auto execution_result = context_engine_.execute_with_snapshot(
            [this, node](const Context& ctx) {
                // 对于 GENERATE_SUBGRAPH，注入 available_subgraphs
                if (node->type == NodeType::GENERATE_SUBGRAPH) {
                    const GenerateSubgraphNode* gsn = static_cast<const GenerateSubgraphNode*>(node);
                    std::string rendered_prompt = this->inject_subgraphs_into_prompt(gsn->prompt_template, ctx);
                    Context new_ctx = ctx;
                    new_ctx["__rendered_prompt__"] = rendered_prompt; // 临时存储
                    return node_executor_.execute_node(node, new_ctx);
                }
                return node_executor_.execute_node(node, ctx);
            },
            context_with_resources,
            snapshot_needed,
            node->path
        );

        result.new_context = std::move(execution_result.new_context);
        result.snapshot_key = execution_result.snapshot_key;
/*
        if (node->type == NodeType::GENERATE_SUBGRAPH) {
            // Use the callback-aware execution method
            result.new_context = execute_generate_subgraph_with_callback(dynamic_cast<const GenerateSubgraphNode*>(node), context_with_resources);
        } else {
            result.new_context = node_executor_.execute_node(node, context_with_resources);
        }
        // Note: Snapshot key is set above if needed_snapshot was true.
        // */

        // --- v3.1: Check for LLM Call Pause ---
        if (node->type == NodeType::LLM_CALL) {
             result.paused_at = node->path;
             // For this synchronous executor, we just return here.
             // An async executor would handle pausing differently.
        }

    } catch (const std::exception& e) {
        result.success = false;
        result.message = std::string("Node execution failed: ") + e.what();
    }

    // 4. 记录 Trace 结束
    nlohmann::json final_ctx_json = result.new_context;
    trace_exporter_.on_node_end(
        node->path,
        result.success ? "success" : "failed",
        result.success ? std::nullopt : std::make_optional(result.message),
        initial_ctx_json,
        final_ctx_json,
        result.snapshot_key,
        budget_controller_.get_budget()
    );

    return result;
}
void ExecutionSession::check_and_requeue_dynamic_deps(const std::unordered_set<NodePath>& newly_executed_nodes) {
    std::vector<NodePath> satisfied_nodes;
    for (auto it = pending_dynamic_deps_.begin(); it != pending_dynamic_deps_.end();) {
        NodePath node_path = it->first;
        std::vector<NodePath>& deps = it->second;

        // Remove deps that were just executed
        deps.erase(std::remove_if(deps.begin(), deps.end(),
                                  [&newly_executed_nodes](const NodePath& dep) {
                                      return newly_executed_nodes.count(dep) > 0;
                                  }),
                   deps.end());

        if (deps.empty()) {
            // All dependencies for this node are now satisfied
            satisfied_nodes.push_back(node_path);
            it = pending_dynamic_deps_.erase(it); // Remove from pending list
        } else {
            ++it;
        }
    }

}

bool ExecutionSession::is_budget_exceeded() const {
    return budget_controller_.exceeded();
}

const TraceExporter& ExecutionSession::get_trace_exporter() const {
    return trace_exporter_;
}

const BudgetController& ExecutionSession::get_budget_controller() const {
    return budget_controller_;
}

const ContextEngine& ExecutionSession::get_context_engine() const {
    return context_engine_;
}

bool ExecutionSession::needs_snapshot(Node* node) const {
    // v3.1: Automatic snapshot triggers
    if (node->metadata.contains("snapshot_before_execution") && 
        node->metadata["snapshot_before_execution"].get<bool>()) {
        return true;
    }

    // Check for a hypothetical 'assert' node type (requires enum update)
    // if (node->type == NodeType::ASSERT) { // Assuming this enum value exists
    //     return true;
    // }

    // Check for tool_call with rollback flag (requires metadata field)
    if (node->type == NodeType::TOOL_CALL) {
        if (node->metadata.contains("rollback_on_failure") && node->metadata["rollback_on_failure"].get<bool>()) {
            return true;
        }
    }

    // v3.1: Fork, GenerateSubgraph, Assert always trigger
    if (node->type == NodeType::FORK || node->type == NodeType::GENERATE_SUBGRAPH || node->type == NodeType::ASSERT /* Assume assert exists */) {
        return true;
    }
    return false;
}

std::vector<NodePath> ExecutionSession::parse_dynamic_wait_for(const nlohmann::json& expr, const Context& ctx) {
    std::vector<NodePath> result;
    if (expr.is_string()) {
        std::string rendered = InjaTemplateRenderer::render(expr.get<std::string>(), ctx);
        try {
            auto arr = nlohmann::json::parse(rendered);
            if (arr.is_array()) {
                for (const auto& item : arr) {
                    if (item.is_string()) result.push_back(item.get<std::string>());
                }
            } else if (arr.is_string()) {
                 result.push_back(arr.get<std::string>()); // Single path
            }
        } catch (...) {
            // If parsing fails, treat as single path
            result.push_back(rendered);
        }
    } else if (expr.is_array()) {
        for (const auto& item : expr) {
            if (item.is_string()) {
                result.push_back(InjaTemplateRenderer::render(item.get<std::string>(), ctx));
            }
        }
    }
    return result;
}


} // namespace agenticdsl
