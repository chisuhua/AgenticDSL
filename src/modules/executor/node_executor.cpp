// modules/executor/src/node_executor.cpp
#include "executor/node_executor.h"
#include "common/utils/template_renderer.h" // 引入 InjaTemplateRenderer (for rendering)
#include "modules/parser/markdown_parser.h" // ← 新增：包含 MarkdownParser
#include <stdexcept>
#include <inja/inja.hpp> // For RenderError
#include <algorithm> // For std::find
#include <thread> // For std::this_thread::sleep_for (if needed for mock)
#include <chrono> // For std::chrono_literals

namespace agenticdsl {

NodeExecutor::NodeExecutor(ToolRegistry& tool_registry, LlamaAdapter* llm_adapter)
    : tool_registry_(tool_registry), llm_adapter_(llm_adapter), markdown_parser_() {
    // llm_adapter_ 可能为 nullptr，NodeExecutor 需要处理这种情况
}

Context NodeExecutor::execute_node(Node* node, const Context& ctx) {
    // 注入资源上下文
    Context context_with_resources = ctx;
    //auto resources_ctx = ResourceManager::instance().get_resources_context();
    //if (!resources_ctx.empty()) {
    //    context_with_resources["resources"] = resources_ctx;
    //}

    // 检查权限
    check_permissions(node->permissions, node->path);

    // 根据节点类型分发执行
    switch (node->type) {
        case NodeType::START:
            return execute_start(dynamic_cast<const StartNode*>(node), context_with_resources);
        case NodeType::END:
            return execute_end(dynamic_cast<const EndNode*>(node), context_with_resources);
        case NodeType::ASSIGN:
            return execute_assign(dynamic_cast<const AssignNode*>(node), context_with_resources);
        case NodeType::LLM_CALL:
            return execute_llm_call(dynamic_cast<const LLMCallNode*>(node), context_with_resources);
        case NodeType::TOOL_CALL:
            return execute_tool_call(dynamic_cast<const ToolCallNode*>(node), context_with_resources);
        case NodeType::RESOURCE:
            return execute_resource(dynamic_cast<const ResourceNode*>(node), context_with_resources);
        case NodeType::FORK:
            return execute_fork(dynamic_cast<const ForkNode*>(node), context_with_resources);
        case NodeType::JOIN:
            return execute_join(dynamic_cast<const JoinNode*>(node), context_with_resources);
        case NodeType::GENERATE_SUBGRAPH:
            return execute_generate_subgraph(dynamic_cast<const GenerateSubgraphNode*>(node), context_with_resources);
        case NodeType::ASSERT:
            return execute_assert(dynamic_cast<const AssertNode*>(node), context_with_resources);
        default:
            throw std::runtime_error("Unknown node type during execution: " + std::to_string(static_cast<int>(node->type)));
    }
}

void NodeExecutor::check_permissions(const std::vector<std::string>& perms, const NodePath& node_path) {
    // 简单实现：检查是否有权限要求，如果有，假设都需要（实际应用中需更细粒度）
    for (const auto& perm : perms) {
        // Example: "tool: web_search"
        if (perm.substr(0, 5) == "tool:") {
            std::string tool_name = perm.substr(6); // Extract name after "tool: "
            if (!tool_registry_.has_tool(tool_name)) {
                throw std::runtime_error("Permission denied: Tool '" + tool_name + "' not available for node: " + node_path);
            }
        }
        // Add more permission checks as needed (network, file, etc.)
    }
    // 例如，检查 perms 中是否包含 "network" 或 "tool: web_search" 等
    // 这里只是示例，实际权限检查逻辑会更复杂
}

Context NodeExecutor::execute_start(const StartNode* node, const Context& ctx) {
    // Start 节点通常不修改上下文，直接返回
    return ctx;
}

Context NodeExecutor::execute_end(const EndNode* node, const Context& ctx) {
    // End 节点通常不修改上下文，直接返回
    // 其终止逻辑由 TopoScheduler 处理
    return ctx;
}

Context NodeExecutor::execute_assign(const AssignNode* node, const Context& ctx) {
    Context new_context = ctx;
    for (const auto& [key, template_str] : node->assign) {
        try {
            std::string rendered_value = InjaTemplateRenderer::render(template_str, ctx);
            new_context[key] = rendered_value; // 赋值到新的上下文
        } catch (const inja::RenderError& e) {
            throw std::runtime_error("Template rendering failed for key '" + key + "': " + std::string(e.what()));
        }
    }
    return new_context;
}

Context NodeExecutor::execute_llm_call(const LLMCallNode* node, const Context& ctx) {
    if (!llm_adapter_) {
        throw std::runtime_error("LLM adapter not available for node: " + node->path);
    }

    Context new_context = ctx;
    try {
        // 使用 PromptBuilder 注入库信息
        std::string rendered_prompt = InjaTemplateRenderer::render(node->prompt_template, ctx);

        std::string llm_response = llm_adapter_->generate(rendered_prompt);

        // 将 LLM 响应赋值到上下文
        if (!node->output_keys.empty()) {
            new_context[node->output_keys[0]] = llm_response;
        } else {
            // 如果没有 output_keys，可能需要特殊处理，或者规范应强制要求
            // 此处假设至少有一个 output_key
            throw std::runtime_error("LLMCallNode has no output_keys: " + node->path);
        }
    } catch (const inja::RenderError& e) {
        throw std::runtime_error("Prompt template rendering failed for node '" + node->path + "': " + std::string(e.what()));
    }
    return new_context;
}

Context NodeExecutor::execute_tool_call(const ToolCallNode* node, const Context& ctx) {
    Context new_context = ctx;

    // 渲染参数
    std::unordered_map<std::string, std::string> rendered_args;
    for (const auto& [key, tmpl] : node->arguments) {
        rendered_args[key] = InjaTemplateRenderer::render(tmpl, ctx);
    }

    // 调用工具
    if (!tool_registry_.has_tool(node->tool_name)) {
        throw std::runtime_error("Tool '" + node->tool_name + "' not registered for node: " + node->path);
    }

    nlohmann::json result = tool_registry_.call_tool(node->tool_name, rendered_args);

    // 处理 output_keys
    if (node->output_keys.size() == 1) {
        new_context[node->output_keys[0]] = result;
    } else if (result.is_object()) {
        for (const auto& key : node->output_keys) {
            if (result.contains(key)) {
                new_context[key] = result[key];
            }
        }
    } else {
        // 如果 result 不是对象，或者 output_keys 多于 1 个，按第一个 key 赋值
        if (!node->output_keys.empty()) {
            new_context[node->output_keys[0]] = result;
        }
    }

    return new_context;
}

Context NodeExecutor::execute_resource(const ResourceNode* node, const Context& ctx) {
    /*
    // 创建 Resource 对象并注册
    Resource resource;
    resource.path = node->path;
    resource.resource_type = node->resource_type;
    resource.uri = node->uri;
    resource.scope = node->scope;
    resource.metadata = node->metadata; // Use node's metadata

    ResourceManager::instance().register_resource(resource);
        */

    // Resource 节点通常不修改上下文，直接返回
    return ctx;
}

Context NodeExecutor::execute_assert(const AssertNode* node, const Context& ctx) {
    // Render the condition expression using the current context
    std::string rendered_condition_str;
    try {
        rendered_condition_str = InjaTemplateRenderer::render(node->condition, ctx);
    } catch (const inja::InjaError& e) {
        throw std::runtime_error("Assert condition template rendering failed for node '" + node->path + "': " + std::string(e.message));
    }

    // Convert rendered result to boolean
    // Inja renders to string, so we check string value
    bool condition_result = false;
    if (rendered_condition_str == "true") {
        condition_result = true;
    } else if (rendered_condition_str == "false") {
        condition_result = false;
    } else {
        // If the rendered result is not explicitly "true" or "false",
        // try to interpret it as a number (0 is false, non-zero is true)
        try {
            double num_val = std::stod(rendered_condition_str);
            condition_result = (num_val != 0.0);
    } catch (...) {
            // If it's not a number either, treat as false or throw error
            // Let's throw an error for non-boolean results
            throw std::runtime_error("Assert condition did not evaluate to a boolean value ('true'/'false' or number): " + rendered_condition_str);
        }
    }

    if (!condition_result) {
        // Condition failed
        if (node->on_failure.has_value()) {
            // This requires the scheduler to handle jumps.
            // For now, throw an error indicating the jump path.
            // The scheduler will catch this and handle the jump.
            throw std::runtime_error("Assert failed. Jumping to: " + node->on_failure.value());
        } else {
            // No jump path, just fail the execution
            throw std::runtime_error("Assert failed at node: " + node->path);
        }
    }
    // Condition passed, context remains unchanged
    return ctx;
}

Context NodeExecutor::execute_fork(const ForkNode* node, const Context& ctx) {
    // ForkNode 的执行逻辑需要由 TopoScheduler 处理，因为它涉及并发和上下文管理。
    // NodeExecutor 本身不能启动新的线程或任务。
    // 因此，这里抛出异常，提示需要在调度器层面实现。
    throw std::runtime_error("ForkNode execution requires concurrent scheduler support, not implemented in NodeExecutor.");
    // In a concurrent scheduler:
    // 1. Create context copies for each branch.
    // 2. Schedule each branch path to run concurrently.
    // 3. Wait for all branches to complete (JoinNode would handle waiting).
    // 4. Trigger snapshot here according to v3.1 spec.
    return ctx; // Placeholder
}

Context NodeExecutor::execute_join(const JoinNode* node, const Context& ctx) {
    // JoinNode 的执行逻辑也需要由 TopoScheduler 处理，因为它需要等待其他分支完成。
    // NodeExecutor 本身无法等待。
    // 因此，这里抛出异常，提示需要在调度器层面实现。
    throw std::runtime_error("JoinNode execution requires concurrent scheduler support, not implemented in NodeExecutor.");
    // In a concurrent scheduler:
    // 1. Wait for all nodes in 'wait_for' to finish.
    // 2. Retrieve their final contexts.
    // 3. Merge them into the current context using 'merge_strategy'.
    // 4. Return the merged context.
    return ctx; // Placeholder
}

Context NodeExecutor::execute_generate_subgraph(const GenerateSubgraphNode* node, const Context& ctx) {
    Context new_context = ctx;
    try {
        if (!ctx.contains("__rendered_prompt__")) {
            throw std::runtime_error("Missing __rendered_prompt__ in context for GenerateSubgraphNode");
        }
        std::string rendered_prompt = ctx.at("__rendered_prompt__").get<std::string>();
        // Add budget info to prompt context if needed by LLM
        //Context prompt_ctx = ctx;
        //prompt_ctx["available_subgraphs"] = PromptBuilder::build_available_libraries_context();
         // Add budget info (nodes_left, depth_left, etc.) - This requires access to ExecutionSession's budget
         // For now, assume budget info is added by the calling context or PromptBuilder
         // prompt_ctx["budget"] = ...; // Access budget from ExecutionSession

        // 2. Call LLM
        std::string generated_dsl;
        if (llm_adapter_) {
            generated_dsl = llm_adapter_->generate(rendered_prompt);
        } else {
            throw std::runtime_error("LLM adapter not available for generate_subgraph");
        }

        // 3. Parse LLM output for `### AgenticDSL '/dynamic/...'` blocks
        // This requires access to the main engine's parser and graph storage mechanism.
        // A reference or callback to the engine might be needed here, or the parsing result is returned.
        // For this executor, assume a global or injected mechanism or return the result for the scheduler to handle.
        // Let's assume the scheduler calls a parser and handles the dynamic graph registration.
        // Here, we just parse and return the generated paths in the context.
        auto new_graphs = markdown_parser_.parse_from_string(generated_dsl); // ← 通过实例调用
        std::vector<std::string> dynamic_paths; // Collect paths of generated graphs
        for (auto& graph : new_graphs) {
            if (graph.path.rfind("/dynamic/", 0) == 0) { // Ensure it's dynamic
                // 4. Validate signature if present (v3.1)
                if (graph.signature.has_value()) {
                    // Perform validation based on signature_validation policy
                    bool is_valid = true; // Placeholder for actual validation logic
                    if (!is_valid && node->signature_validation == "strict") {
                         if (node->on_signature_violation.has_value()) {
                             // Trigger jump to on_signature_violation path
                             // This requires scheduler logic to handle jumps
                             // For now, throw error
                             throw std::runtime_error("Signature validation failed (strict mode) for generated graph: " + graph.path);
                         } else {
                             // Default behavior for strict violation without jump path
                             throw std::runtime_error("Signature validation failed (strict mode) for generated graph: " + graph.path);
                         }
                    } else if (!is_valid && node->signature_validation == "warn") {
                        // Log warning but continue
                        std::cerr << "[WARNING] Signature validation failed (warn mode) for generated graph: " << graph.path << std::endl;
                    } // ignore: do nothing
                }
                dynamic_paths.push_back(graph.path);
                // 5. Register new graph (This logic belongs in the scheduler/engine)
                // g_current_engine->append_graphs({std::move(graph)}); // Placeholder - requires access to engine/scheduler
            }
        }

        // 6. Store generated graph path(s) in context
        if (!node->output_keys.empty()) {
            if (dynamic_paths.size() == 1) {
                new_context[node->output_keys[0]] = dynamic_paths[0];
            } else {
                new_context[node->output_keys[0]] = dynamic_paths; // Store as array if multiple
            }
        }

        // 7. Snapshot trigger happens in ExecutionSession::execute_node, not here.

    } catch (const std::exception & e) {
        throw std::runtime_error("GenerateSubgraphNode execution failed: " + std::string(e.what()));
    }
    return new_context;
}

} // namespace agenticdsl
