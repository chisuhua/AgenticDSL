// modules/executor/include/executor/node_executor.h
#ifndef AGENTICDSL_MODULES_EXECUTOR_NODE_EXECUTOR_H
#define AGENTICDSL_MODULES_EXECUTOR_NODE_EXECUTOR_H

#include "core/types/context.h" // 引入 Context
#include "core/types/node.h"    // 引入 NodePath, Node, NodeType, StartNode, EndNode, etc.
#include "core/types/resource.h" // 引入 ResourceType
#include "common/utils/template_renderer.h" // 引入 InjaTemplateRenderer
#include "common/tools/registry.h" // 引入 ToolRegistry
#include "common/llm/llama_adapter.h" // 引入 LlamaAdapter
//#include "agenticdsl/resources/manager.h" // 引入 ResourceManager
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace agenticdsl {

using AppendGraphsCallback = std::function<void(std::vector<ParsedGraph>)>;

class NodeExecutor {
public:
    NodeExecutor(ToolRegistry& tool_registry, LlamaAdapter* llm_adapter = nullptr);

    // 执行一个节点，返回新的上下文
    Context execute_node(Node* node, const Context& ctx);
    void set_append_graphs_callback(AppendGraphsCallback cb) {
        append_graphs_callback_ = std::move(cb);
    }

private:
    ToolRegistry& tool_registry_;
    LlamaAdapter* llm_adapter_; // 可为 nullptr
    AppendGraphsCallback append_graphs_callback_;

    // 权限检查
    void check_permissions(const std::vector<std::string>& perms, const NodePath& node_path);

    // 内部执行方法，根据节点类型分发
    Context execute_start(const StartNode* node, const Context& ctx);
    Context execute_end(const EndNode* node, const Context& ctx);
    Context execute_assign(const AssignNode* node, const Context& ctx);
    Context execute_llm_call(const LLMCallNode* node, const Context& ctx);
    Context execute_tool_call(const ToolCallNode* node, const Context& ctx);
    Context execute_resource(const ResourceNode* node, const Context& ctx);
};

} // namespace agenticdsl

#endif // AGENTICDSL_MODULES_EXECUTOR_NODE_EXECUTOR_H
