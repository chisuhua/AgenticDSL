#include "agenticdsl/core/nodes.h"
#include "agenticdsl/dsl/templates.h"
#include "agenticdsl/tools/registry.h"
#include "agenticdsl/llm/llama_adapter.h"
#include "agenticdsl/resources/manager.h" // v1.1: New include
#include <stdexcept>

namespace agenticdsl {

// StartNode
StartNode::StartNode(NodePath path, std::vector<NodePath> next_paths)
    : Node{.path = std::move(path), .type = NodeType::START, .next = std::move(next_paths)} {}

Context StartNode::execute(Context& context) {
    return context; // Start节点只是跳转
}

// EndNode
EndNode::EndNode(NodePath path)
    : Node{.path = std::move(path), .type = NodeType::END} {}

Context EndNode::execute(Context& context) {
    return context; // End节点返回当前上下文
}

// AssignNode (v1.1: renamed from SetNode)
AssignNode::AssignNode(NodePath path,
                       std::unordered_map<std::string, std::string> assigns,
                       std::vector<NodePath> next_paths)
    : Node{.path = std::move(path), .type = NodeType::ASSIGN, .next = std::move(next_paths)}, assign(std::move(assigns)) {}

Context AssignNode::execute(Context& context) {
    Context new_context = context; // 复制上下文

    for (const auto& [key, template_str] : assign) {
        try {
            // 使用 inja 渲染模板
            std::string rendered_value = InjaTemplateRenderer::render(template_str, context);
            new_context[key] = rendered_value;
        } catch (const inja::RenderError& e) {
             throw std::runtime_error("Template rendering failed for key '" + key + "': " + e.what());
        }
    }

    return new_context;
}

// LLMCallNode
LLMCallNode::LLMCallNode(NodePath path,
                         std::string prompt,
                         std::vector<std::string> output_keys,
                         std::vector<NodePath> next_paths)
    : Node{.path = std::move(path), .type = NodeType::LLM_CALL, .next = std::move(next_paths)},
      prompt_template(std::move(prompt)), output_keys(std::move(output_keys)) {}

Context LLMCallNode::execute(Context& context) {
    Context new_context = context;

    try {
        // 使用 inja 渲染提示模板
        std::string rendered_prompt = InjaTemplateRenderer::render(prompt_template, context);

        // 这里调用LLM（Phase 1 用模拟）
        // 在实际应用中，这里会调用 LLM 适配器
        std::string llm_response = "[MOCK] Generated response for prompt length: " + std::to_string(rendered_prompt.length());

        // v1.1: Support multiple output keys
        if (this->output_keys.size() == 1) {
            new_context[this->output_keys[0]] = llm_response;
        } else {
            // If multiple keys, store the response under a default key or distribute logic
            // For simplicity, let's store it under the first key
            if (!this->output_keys.empty()) {
                new_context[this->output_keys[0]] = llm_response;
            }
        }

    } catch (const inja::RenderError& e) {
         throw std::runtime_error("Prompt template rendering failed: " + e.what());
    }
    return new_context;
}

// ToolCallNode
ToolCallNode::ToolCallNode(NodePath path,
                           std::string tool,
                           std::unordered_map<std::string, std::string> arguments,
                           std::vector<std::string> output_keys,
                           std::vector<NodePath> next_paths)
    : Node{.path = std::move(path), .type = NodeType::TOOL_CALL, .next = std::move(next_paths)},
      tool_name(std::move(tool)), arguments(std::move(arguments)), output_keys(std::move(output_keys)) {}

Context ToolCallNode::execute(Context& context) {
    Context new_context = context;

    // 渲染参数
    std::unordered_map<std::string, std::string> rendered_args;
    for (const auto& [key, template_str] : arguments) {
        rendered_args[key] = InjaTemplateRenderer::render(template_str, context);
    }

    // 调用工具
    auto registry = &ToolRegistry::instance();
    if (!registry->has_tool(tool_name)) {
         throw std::runtime_error("Tool '" + tool_name + "' not registered");
    }

    nlohmann::json result = registry->call_tool(tool_name, rendered_args);

    // v1.1: Support multiple output keys
    if (this->output_keys.size() == 1) {
        new_context[this->output_keys[0]] = result;
    } else if (result.is_object() && this->output_keys.size() > 1) {
        // If result is an object and we have multiple output keys, try to map them
        for (const auto& output_key : this->output_keys) {
            if (result.contains(output_key)) {
                new_context[output_key] = result[output_key];
            }
        }
    } else if (!this->output_keys.empty()) {
        // Fallback: store the entire result under the first key
        new_context[this->output_keys[0]] = result;
    }

    return new_context;
}

// ResourceNode (v1.1: new)
ResourceNode::ResourceNode(NodePath path,
                           ResourceType type,
                           std::string uri,
                           std::string scope)
    : Node{.path = std::move(path), .type = NodeType::RESOURCE},
      resource_type(type), uri(std::move(uri)), scope(std::move(scope)) {}

Context ResourceNode::execute(Context& context) {
    // This node doesn't modify the main execution context.
    // It registers itself with the ResourceManager.
    Resource resource;
    resource.path = this->path;
    resource.resource_type = this->resource_type;
    resource.uri = this->uri;
    resource.scope = this->scope;
    resource.metadata = this->metadata;

    ResourceManager::instance().register_resource(resource);

    // v1.1: Also inject a read-only reference into the execution context under 'resources'
    // This is done by the executor before running nodes, not here.
    // The executor will merge ResourceManager::get_resources_context() into the context.

    return context; // Return context unchanged for this node type
}

} // namespace agenticdsl
