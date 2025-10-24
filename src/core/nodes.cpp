#include "agenticdsl/core/nodes.h"
#include "agenticdsl/dsl/templates.h"
#include "agenticdsl/tools/registry.h"
#include "agenticdsl/llm/prompt_builder.h"
#include "agenticdsl/llm/llama_adapter.h"
#include "agenticdsl/resources/manager.h"
#include <inja/inja.hpp>
#include <stdexcept>
#include <string>
#include <memory>

namespace agenticdsl {

// ————————————————————————
// StartNode
// ————————————————————————

StartNode::StartNode(NodePath path, std::vector<NodePath> next_paths)
    : Node(std::move(path), NodeType::START, std::move(next_paths)) {}

Context StartNode::execute(Context& context) {
    return context;
}

std::unique_ptr<Node> StartNode::clone() const {
    auto node = std::make_unique<StartNode>(path, next);
    node->metadata = metadata;
    return node;
}

// ————————————————————————
// EndNode
// ————————————————————————

EndNode::EndNode(NodePath path)
    : Node(std::move(path), NodeType::END) {}

Context EndNode::execute(Context& context) {
    return context;
}

std::unique_ptr<Node> EndNode::clone() const {
    auto node = std::make_unique<EndNode>(path);
    node->next = next;
    node->metadata = metadata;
    return node;
}

// ————————————————————————
// AssignNode
// ————————————————————————

AssignNode::AssignNode(NodePath path,
                       std::unordered_map<std::string, std::string> assigns,
                       std::vector<NodePath> next_paths)
    : Node(std::move(path), NodeType::ASSIGN, std::move(next_paths)),
      assign(std::move(assigns)) {}

Context AssignNode::execute(Context& context) {
    Context new_context = context;
    for (const auto& [key, template_str] : assign) {
        try {
            std::string rendered_value = InjaTemplateRenderer::render(template_str, context);
            new_context[key] = rendered_value;
        } catch (const inja::RenderError& e) {
            throw std::runtime_error("Template rendering failed for key '" + key + "': " + std::string(e.what()));
        }
    }
    return new_context;
}

std::unique_ptr<Node> AssignNode::clone() const {
    auto node = std::make_unique<AssignNode>(path, assign, next);
    node->metadata = metadata;
    return node;
}

// ————————————————————————
// LLMCallNode
// ————————————————————————

LLMCallNode::LLMCallNode(NodePath path,
                         std::string prompt,
                         std::vector<std::string> output_keys,
                         std::vector<NodePath> next_paths)
    : Node(std::move(path), NodeType::LLM_CALL, std::move(next_paths)),
      prompt_template(std::move(prompt)),
      output_keys(std::move(output_keys)) {
    if (this->output_keys.empty()) {
        throw std::invalid_argument("LLMCallNode requires at least one output_key");
    }
}

Context LLMCallNode::execute(Context& context) {
    Context new_context = context;
    try {
        //std::string rendered_prompt = InjaTemplateRenderer::render(prompt_template, context);
        std::string rendered_prompt = PromptBuilder::inject_libraries_into_prompt(prompt_template, context);

        std::string llm_response;
        if (g_current_llm_adapter) {
            llm_response = g_current_llm_adapter->generate(rendered_prompt);
        } else {
            llm_response = "[ERROR] LLM adapter not available";
        }

        // TODO: Replace mock with real LLM call via engine
        //std::string llm_response = "[MOCK] Generated response for prompt length: " +
        //                           std::to_string(rendered_prompt.length());
        new_context[output_keys[0]] = llm_response;
    } catch (const inja::RenderError& e) {
        throw std::runtime_error("Prompt template rendering failed: " + std::string(e.what()));
    }
    return new_context;
}

std::unique_ptr<Node> LLMCallNode::clone() const {
    auto node = std::make_unique<LLMCallNode>(path, prompt_template, output_keys, next);
    node->metadata = metadata;
    return node;
}

// ————————————————————————
// ToolCallNode
// ————————————————————————

ToolCallNode::ToolCallNode(NodePath path,
                           std::string tool_name,
                           std::unordered_map<std::string, std::string> arguments,
                           std::vector<std::string> output_keys,
                           std::vector<NodePath> next_paths)
    : Node(std::move(path), NodeType::TOOL_CALL, std::move(next_paths)),
      tool_name(std::move(tool_name)),
      arguments(std::move(arguments)),
      output_keys(std::move(output_keys)) {
    if (this->output_keys.empty()) {
        throw std::invalid_argument("ToolCallNode requires at least one output_key");
    }
}

Context ToolCallNode::execute(Context& context) {
    Context new_context = context;

    // Render arguments
    std::unordered_map<std::string, std::string> rendered_args;
    for (const auto& [key, tmpl] : arguments) {
        rendered_args[key] = InjaTemplateRenderer::render(tmpl, context);
    }

    // Call tool
    auto& registry = ToolRegistry::instance();
    if (!registry.has_tool(tool_name)) {
        throw std::runtime_error("Tool '" + tool_name + "' not registered");
    }

    nlohmann::json result = registry.call_tool(tool_name, rendered_args);

    // Handle output_keys per v1.1 spec
    if (output_keys.size() == 1) {
        new_context[output_keys[0]] = result;
    } else if (result.is_object()) {
        for (const auto& key : output_keys) {
            if (result.contains(key)) {
                new_context[key] = result[key];
            }
        }
    } else {
        new_context[output_keys[0]] = result;
    }

    return new_context;
}

std::unique_ptr<Node> ToolCallNode::clone() const {
    auto node = std::make_unique<ToolCallNode>(path, tool_name, arguments, output_keys, next);
    node->metadata = metadata;
    return node;
}

// ————————————————————————
// ResourceNode
// ————————————————————————

ResourceNode::ResourceNode(NodePath path,
                           ResourceType type,
                           std::string uri,
                           std::string scope,
                           nlohmann::json metadata)
    : Node(std::move(path), NodeType::RESOURCE, {}, std::move(metadata)),
      resource_type(type),
      uri(std::move(uri)),
      scope(std::move(scope)) {}

Context ResourceNode::execute(Context& context) {
    Resource resource;
    resource.path = this->path;
    resource.resource_type = this->resource_type;
    resource.uri = this->uri;
    resource.scope = this->scope;
    resource.metadata = this->metadata;

    ResourceManager::instance().register_resource(resource);

    return context;
}

std::unique_ptr<Node> ResourceNode::clone() const {
    auto node = std::make_unique<ResourceNode>(path, resource_type, uri, scope, metadata);
    return node;
}

} // namespace agenticdsl
