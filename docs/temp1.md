

## 项目架构 (v1.1)

```
AgenticDSL/
├── include/
│   ├── agenticdsl/
│   │   ├── core/
│   │   │   ├── engine.h
│   │   │   ├── parser.h
│   │   │   ├── executor.h
│   │   │   └── nodes.h
│   │   ├── dsl/
│   │   │   ├── spec.h
│   │   │   └── templates.h
│   │   ├── tools/
│   │   │   └── registry.h
│   │   ├── resources/
│   │   │   └── manager.h
│   │   └── llm/
│   │       └── llama_adapter.h
│   └── common/
│       ├── types.h
│       └── utils.h
├── src/
│   ├── core/
│   │   ├── engine.cpp
│   │   ├── parser.cpp
│   │   ├── executor.cpp
│   │   └── nodes.cpp
│   ├── dsl/
│   │   └── templates.cpp
│   ├── tools/
│   │   └── registry.cpp
│   ├── resources/
│   │   └── manager.cpp
│   ├── llm/
│   │   └── llama_adapter.cpp
│   └── main.cpp
├── examples/
│   └── agent_loop_v11_example.cpp
├── tests/
│   └── test_v11_basic.cpp
├── models/
├── CMakeLists.txt
└── README.md
```

## 核心实现

### 1. 类型定义 (`include/common/types.h`)

```c++
#ifndef AGENFLOW_TYPES_H
#define AGENFLOW_TYPES_H

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>
#include <variant>
#include <concepts>
#include <string_view>
#include <nlohmann/json.hpp> // inja 依赖 nlohmann/json
#include <optional>

namespace agenticdsl {

// 使用 nlohmann::json 作为统一的数据类型
using Value = nlohmann::json;
using Context = nlohmann::json;

// 节点ID类型 - 现在对应路径
using NodePath = std::string; // e.g., "/main/step1"

// 节点类型枚举
enum class NodeType : uint8_t {
    START,
    END,
    ASSIGN, // v1.1: renamed from SET
    LLM_CALL,
    TOOL_CALL,
    RESOURCE // v1.1: new type
};

// 资源类型枚举
enum class ResourceType : uint8_t {
    FILE,
    POSTGRES,
    MYSQL,
    SQLITE,
    API_ENDPOINT,
    VECTOR_STORE,
    CUSTOM
};

// 工具函数类型 - 现在使用 nlohmann::json
using ToolFunction = std::function<nlohmann::json(const std::unordered_map<std::string, std::string>&)>;

// 执行结果
struct ExecutionResult {
    bool success;
    std::string message; // 错误信息或成功信息
    Context final_context; // 执行结束时的上下文
};

// 资源定义
struct Resource {
    NodePath path; // e.g., /resources/weather_cache
    ResourceType resource_type;
    std::string uri;
    std::string scope; // "global" or "local"
    nlohmann::json metadata; // optional
};

} // namespace agenticdsl

#endif
```

### 2. 工具函数 (`include/common/utils.h`)

```c++
#ifndef AGENFLOW_UTILS_H
#define AGENFLOW_UTILS_H

#include "types.h"
#include <string>
#include <sstream>
#include <vector>
#include <regex>
#include <filesystem>

namespace agenticdsl {

// 从字符串中提取 ```yaml ... ``` 代码块
inline std::string extract_yaml_block(const std::string& text) {
    std::regex yaml_pattern(R"(```\s*yaml\s*\n(.*?)\n```)", std::regex_constants::dotall);
    std::smatch match;
    if (std::regex_search(text, match, yaml_pattern)) {
        return match[1].str();
    }
    return "";
}

// 从 Markdown 内容中提取路径化块
inline std::vector<std::pair<NodePath, std::string>> extract_pathed_blocks(const std::string& markdown_content) {
    std::vector<std::pair<NodePath, std::string>> blocks;
    std::regex block_header_pattern(R"(###\s+AgenticDSL\s+`(/[\w/\-]*)`)");
    std::regex yaml_content_pattern(R"(\# --- BEGIN AgenticDSL ---\s*\n(.*?)\n\# --- END AgenticDSL ---)", std::regex_constants::dotall);

    std::istringstream iss(markdown_content);
    std::string line;
    std::string current_path;
    std::string current_content;
    bool in_block = false;

    while (std::getline(iss, line)) {
        std::smatch header_match;
        if (std::regex_match(line, header_match, block_header_pattern)) {
            if (in_block && !current_path.empty()) {
                blocks.push_back({current_path, current_content});
            }
            current_path = header_match[1].str();
            current_content.clear();
            in_block = true;
        } else if (in_block) {
            std::smatch yaml_match;
            if (std::regex_match(line, yaml_match, yaml_content_pattern)) {
                 current_content = yaml_match[1].str();
                 in_block = false;
            } else {
                current_content += line + "\n";
            }
        }
    }

    // Add the last block
    if (!current_path.empty() && !current_content.empty()) {
        blocks.push_back({current_path, current_content});
    }

    return blocks;
}

// 检查 Context 中是否存在嵌套路径 (e.g., "user.profile.name")
inline bool context_has_path(const Context& ctx, const std::string& path) {
    std::istringstream iss(path);
    std::string segment;
    std::getline(iss, segment, '.');
    
    auto current = ctx;
    if (current.contains(segment)) {
        current = current[segment];
    } else {
        return false;
    }

    while (std::getline(iss, segment, '.')) {
        if (current.is_object() && current.contains(segment)) {
            current = current[segment];
        } else {
            return false;
        }
    }
    return true;
}

// 从 Context 中获取嵌套路径的值 (e.g., "user.profile.name")
inline nlohmann::json get_context_value(const Context& ctx, const std::string& path) {
    std::istringstream iss(path);
    std::string segment;
    std::getline(iss, segment, '.');
    
    auto current = ctx;
    if (current.contains(segment)) {
        current = current[segment];
    } else {
        return nlohmann::json(); // Return null if path not found
    }

    while (std::getline(iss, segment, '.')) {
        if (current.is_object() && current.contains(segment)) {
            current = current[segment];
        } else {
            return nlohmann::json(); // Return null if path not found
        }
    }
    return current;
}

// 检查字符串是否为有效的 JSON
inline bool is_valid_json(const std::string& str) {
    try {
        nlohmann::json::parse(str);
        return true;
    } catch (const nlohmann::json::parse_error&) {
        return false;
    }
}

} // namespace agenticdsl

#endif
```

### 3. DSL规范 (`include/agenticdsl/dsl/spec.h`)

```c++
#ifndef AGENFLOW_SPEC_H
#define AGENFLOW_SPEC_H

#include "common/types.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace agenticdsl {

class DSLValidator {
public:
    static bool validate_nodes(const std::vector<nlohmann::json>& nodes_json);
    static bool validate_node_type(const nlohmann::json& node_json);
    static bool validate_graph_structure(const std::vector<nlohmann::json>& nodes_json);

    // v1.1: New validators
    static bool validate_resource_node(const nlohmann::json& node_json);
    static bool validate_path(const std::string& path);

private:
    static bool validate_start_node(const nlohmann::json& node_json);
    static bool validate_end_node(const nlohmann::json& node_json);
    static bool validate_assign_node(const nlohmann::json& node_json); // v1.1: renamed from set
    static bool validate_llm_call_node(const nlohmann::json& node_json);
    static bool validate_tool_call_node(const nlohmann::json& node_json);
};

} // namespace agenticdsl

#endif
```

### 4. Inja 模板渲染器 (`include/agenticdsl/dsl/templates.h`)

```c++
#ifndef AGENFLOW_TEMPLATES_H
#define AGENFLOW_TEMPLATES_H

#include "common/types.h"
#include <inja/inja.hpp>
#include <string>
#include <string_view>

namespace agenticdsl {

class InjaTemplateRenderer {
public:
    InjaTemplateRenderer();

    // Basic rendering with safety checks
    static std::string render(std::string_view template_str, const Context& context);

    // Rendering with instance environment (for callbacks)
    std::string render_with_env(std::string_view template_str, const Context& context);

    // Register custom callbacks (for v2+ or advanced features)
    template<typename Func>
    void add_callback(const std::string& name, size_t num_args, Func&& func);

    template<typename Func>
    void add_void_callback(const std::string& name, size_t num_args, Func&& func);

    // Get environment reference for advanced configuration
    inja::Environment& get_environment() { return env_; }

private:
    inja::Environment env_;

    // Configure environment for security (disable includes, etc.)
    void configure_security();
};

} // namespace agenticdsl

#endif
```

### 5. 资源管理器 (`include/agenticdsl/resources/manager.h`)

```c++
#ifndef AGENFLOW_RESOURCE_MANAGER_H
#define AGENFLOW_RESOURCE_MANAGER_H

#include "common/types.h"
#include <unordered_map>
#include <string>

namespace agenticdsl {

class ResourceManager {
public:
    static ResourceManager& instance();

    void register_resource(const Resource& resource);
    bool has_resource(const NodePath& path) const;
    const Resource* get_resource(const NodePath& path) const;
    nlohmann::json get_resources_context() const; // For injecting into execution context

private:
    ResourceManager() = default;
    std::unordered_map<NodePath, Resource> resources_;
};

} // namespace agenticdsl

#endif
```

### 6. 节点定义 (`include/agenticdsl/core/nodes.h`)

```c++
#ifndef AGENFLOW_NODES_H
#define AGENFLOW_NODES_H

#include "common/types.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <optional>
#include <vector>

namespace agenticdsl {

// 前向声明渲染器
class InjaTemplateRenderer;

// 节点基类
struct Node {
    NodePath path; // v1.1: Use path instead of id
    NodeType type;
    std::vector<NodePath> next; // v1.1: Allow multiple next paths
    nlohmann::json metadata; // Optional metadata

    virtual ~Node() = default;
    virtual Context execute(Context& context) = 0;
};

// Start节点
struct StartNode : public Node {
    StartNode(NodePath path, std::vector<NodePath> next_paths = {});
    Context execute(Context& context) override;
};

// End节点
struct EndNode : public Node {
    EndNode(NodePath path);
    Context execute(Context& context) override;
};

// Assign节点 - 使用 inja 渲染 (v1.1: renamed from Set)
struct AssignNode : public Node {
    std::unordered_map<std::string, std::string> assign; // key -> Inja template_string

    AssignNode(NodePath path,
               std::unordered_map<std::string, std::string> assigns,
               std::vector<NodePath> next_paths = {});
    Context execute(Context& context) override;
};

// LLM调用节点
struct LLMCallNode : public Node {
    std::string prompt_template; // Inja template
    std::vector<std::string> output_keys; // v1.1: Allow multiple output keys

    LLMCallNode(NodePath path,
                std::string prompt,
                std::vector<std::string> output_keys,
                std::vector<NodePath> next_paths = {});
    Context execute(Context& context) override;
};

// 工具调用节点
struct ToolCallNode : public Node {
    std::string tool_name;
    std::unordered_map<std::string, std::string> arguments; // arg_name -> Inja template_string
    std::vector<std::string> output_keys; // v1.1: Allow multiple output keys

    ToolCallNode(NodePath path,
                 std::string tool,
                 std::unordered_map<std::string, std::string> arguments,
                 std::vector<std::string> output_keys,
                 std::vector<NodePath> next_paths = {});
    Context execute(Context& context) override;
};

// Resource节点 (v1.1: new)
struct ResourceNode : public Node {
    ResourceType resource_type;
    std::string uri;
    std::string scope; // "global" or "local"

    ResourceNode(NodePath path,
                 ResourceType type,
                 std::string uri,
                 std::string scope = "global");
    Context execute(Context& context) override; // This node just registers itself
};

} // namespace agenticdsl

#endif
```

### 7. 工具注册器 (`include/agenticdsl/tools/registry.h`)

```c++
#ifndef AGENFLOW_REGISTRY_H
#define AGENFLOW_REGISTRY_H

#include "common/types.h"
#include <unordered_map>
#include <functional>
#include <string_view>
#include <vector>

namespace agenticdsl {

class ToolRegistry {
public:
    static ToolRegistry& instance();

    template<typename Func>
    requires std::invocable<Func, const std::unordered_map<std::string, std::string>&>
    void register_tool(std::string_view name, Func&& func);

    bool has_tool(std::string_view name) const;
    nlohmann::json call_tool(std::string_view name, const std::unordered_map<std::string, std::string>& args);
    std::vector<std::string> list_tools() const;

private:
    ToolRegistry() = default;
    std::unordered_map<std::string, std::function<nlohmann::json(const std::unordered_map<std::string, std::string>&)>> tools_;

    static bool register_default_tools();
    static bool init_default_tools;
};

} // namespace agenticdsl

#endif
```

### 8. LLM 适配器 (`include/agenticdsl/llm/llama_adapter.h`)

```c++
#ifndef AGENFLOW_LLAMA_ADAPTER_H
#define AGENFLOW_LLAMA_ADAPTER_H

#include "common/types.h"
#include <string>
#include <memory>
#include <vector>
#include <llama.h> // llama.cpp header

namespace agenticdsl {

class LlamaAdapter {
public:
    struct Config {
        std::string model_path;
        int n_ctx = 2048;
        int n_threads = 4;
        float temperature = 0.7f;
        int n_predict = 512;
        std::string system_prompt = "You are a helpful assistant that generates AgenticDSL code.";
    };

    LlamaAdapter(const Config& config);
    ~LlamaAdapter();

    // 生成文本
    std::string generate(const std::string& prompt);

    // 批量生成（用于并发）
    std::vector<std::string> generate_batch(const std::vector<std::string>& prompts);

    // 检查模型是否加载成功
    bool is_loaded() const;

    // 获取当前上下文（用于调试）
    std::string get_context();

private:
    Config config_;
    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    std::string system_prompt_;

    void load_model();
    std::string tokenize_and_generate(const std::string& prompt);
    std::vector<llama_token> tokenize(const std::string& text);
    std::string detokenize(const std::vector<llama_token>& tokens);
};

} // namespace agenticdsl

#endif
```

### 9. 解析器 (`include/agenticdsl/core/parser.h`)

```c++
#ifndef AGENFLOW_PARSER_H
#define AGENFLOW_PARSER_H

#include "nodes.h"
#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>

namespace agenticdsl {

struct ParsedGraph {
    std::vector<std::unique_ptr<Node>> nodes;
    NodePath path; // e.g., /main
    nlohmann::json metadata; // graph-level metadata
};

class MarkdownParser {
public:
    std::vector<ParsedGraph> parse_from_string(const std::string& markdown_content);
    std::vector<ParsedGraph> parse_from_file(const std::string& file_path);

private:
    std::vector<std::pair<NodePath, std::string>> extract_pathed_blocks(const std::string& content);
    std::unique_ptr<Node> create_node_from_json(const NodePath& path, const nlohmann::json& node_json);
    void validate_nodes(const std::vector<std::unique_ptr<Node>>& nodes);
};

} // namespace agenticdsl

#endif
```

### 10. 执行器 (`include/agenticdsl/core/executor.h`)

```c++
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
```

### 11. 主引擎 (`include/agenticdsl/core/engine.h`)

```c++
#ifndef AGENFLOW_ENGINE_H
#define AGENFLOW_ENGINE_H

#include "executor.h"
#include "parser.h"
#include "llm/llama_adapter.h"
#include <memory>
#include <concepts>

namespace agenticdsl {

class AgenticDSLEngine {
public:
    static std::unique_ptr<AgenticDSLEngine> from_markdown(const std::string& markdown_content,
                                                          const Context& initial_context = Context{});
    static std::unique_ptr<AgenticDSLEngine> from_file(const std::string& file_path,
                                                      const Context& initial_context = Context{});

    ExecutionResult run(const Context& context = Context{});

    template<typename Func>
    requires std::invocable<Func, const std::unordered_map<std::string, std::string>&>
    void register_tool(std::string_view name, Func&& func);

    // 获取引擎的 LLM 适配器
    LlamaAdapter* get_llm_adapter() { return llama_adapter_.get(); }

private:
    std::unique_ptr<ModernFlowExecutor> executor_;
    std::unique_ptr<LlamaAdapter> llama_adapter_;

    AgenticDSLEngine(std::vector<std::unique_ptr<Node>> nodes);
};

} // namespace agenticdsl

#endif
```

---

## .cpp 文件实现

### 1. 节点实现 (`src/core/nodes.cpp`)

```c++
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
```

### 2. 模板渲染器实现 (`src/dsl/templates.cpp`)

```c++
#include "agenticdsl/dsl/templates.h"
#include <inja/inja.hpp>
#include <exception>

namespace agenticdsl {

InjaTemplateRenderer::InjaTemplateRenderer() : env_() {
    // Set default delimiters
    env_.set_expression("{{", "}}");
    env_.set_statement("{%", "%}");
    env_.set_comment("{#", "#}");
    env_.set_line_statement("##");

    // Configure for security (v1.1)
    configure_security();
}

void InjaTemplateRenderer::configure_security() {
    // Disable include and extend functionality
    env_.set_include_callback([](const std::filesystem::path&, const std::string& name) {
        throw inja::RenderError("Include functionality is disabled for security.");
    });
    // Note: There's no direct API to disable 'extends', but it's rarely used in simple templates.
    // The main risk is 'include', which is now disabled.
    // Additional security could involve sandboxing or pre-parsing templates to remove disallowed constructs.
}

std::string InjaTemplateRenderer::render(std::string_view template_str, const Context& context) {
    static InjaTemplateRenderer renderer; // Use static instance with security config
    try {
        inja::Template temp = renderer.env_.parse(template_str);
        return renderer.env_.render(temp, context);
    } catch (const std::exception& e) {
        // In production, might want more detailed error logging
        return std::string(template_str);
    }
}

std::string InjaTemplateRenderer::render_with_env(std::string_view template_str, const Context& context) {
    try {
        inja::Template temp = env_.parse(template_str);
        return env_.render(temp, context);
    } catch (const std::exception& e) {
        return std::string(template_str);
    }
}

template<typename Func>
void InjaTemplateRenderer::add_callback(const std::string& name, size_t num_args, Func&& func) {
    env_.add_callback(name, num_args, std::forward<Func>(func));
}

template<typename Func>
void InjaTemplateRenderer::add_void_callback(const std::string& name, size_t num_args, Func&& func) {
    env_.add_void_callback(name, num_args, std::forward<Func>(func));
}

// Instantiate templates
template void InjaTemplateRenderer::add_callback<std::function<nlohmann::json(inja::Arguments&)>>(const std::string&, size_t, std::function<nlohmann::json(inja::Arguments&)>);
template void InjaTemplateRenderer::add_void_callback<std::function<void(inja::Arguments&)>>(const std::string&, size_t, std::function<void(inja::Arguments&)>);

} // namespace agenticdsl
```

### 3. 资源管理器实现 (`src/resources/manager.cpp`)

```c++
#include "agenticdsl/resources/manager.h"
#include <stdexcept>

namespace agenticdsl {

ResourceManager& ResourceManager::instance() {
    static ResourceManager manager;
    return manager;
}

void ResourceManager::register_resource(const Resource& resource) {
    resources_[resource.path] = resource;
}

bool ResourceManager::has_resource(const NodePath& path) const {
    return resources_.find(path) != resources_.end();
}

const Resource* ResourceManager::get_resource(const NodePath& path) const {
    auto it = resources_.find(path);
    return (it != resources_.end()) ? &it->second : nullptr;
}

nlohmann::json ResourceManager::get_resources_context() const {
    nlohmann::json resources_ctx = nlohmann::json::object();
    for (const auto& [path, resource] : resources_) {
        // Create a simplified JSON representation for the context
        nlohmann::json resource_info;
        resource_info["uri"] = resource.uri;
        resource_info["type"] = static_cast<int>(resource.resource_type);
        resource_info["scope"] = resource.scope;
        // Add other relevant fields as needed
        resources_ctx[path] = resource_info;
    }
    return resources_ctx;
}

} // namespace agenticdsl
```

### 4. 工具注册器实现 (`src/tools/registry.cpp`)

```c++
#include "agenticdsl/tools/registry.h"
#include <limits>

namespace agenticdsl {

ToolRegistry& ToolRegistry::instance() {
    static ToolRegistry registry;
    return registry;
}

bool ToolRegistry::has_tool(std::string_view name) const {
    return tools_.find(std::string(name)) != tools_.end();
}

nlohmann::json ToolRegistry::call_tool(std::string_view name, const std::unordered_map<std::string, std::string>& args) {
    auto it = tools_.find(std::string(name));
    if (it == tools_.end()) {
        throw std::runtime_error("Tool '" + std::string(name) + "' not found");
    }
    return it->second(args);
}

std::vector<std::string> ToolRegistry::list_tools() const {
    std::vector<std::string> names;
    names.reserve(tools_.size());
    for (const auto& [name, _] : tools_) {
        names.push_back(name);
    }
    return names;
}

bool ToolRegistry::register_default_tools() {
    instance().register_tool("web_search", [](const std::unordered_map<std::string, std::string>& args) -> nlohmann::json {
        auto query_it = args.find("query");
        std::string query = query_it != args.end() ? query_it->second : "default query";
        return std::string("[MOCK] Search results for: ") + query;
    });

    instance().register_tool("get_weather", [](const std::unordered_map<std::string, std::string>& args) -> nlohmann::json {
        auto location_it = args.find("location");
        std::string location = location_it != args.end() ? location_it->second : "unknown";
        return std::string("[MOCK] Weather in ") + location + ": Sunny, 22°C";
    });

    instance().register_tool("calculate", [](const std::unordered_map<std::string, std::string>& args) -> nlohmann::json {
        auto a_it = args.find("a");
        auto b_it = args.find("b");
        auto op_it = args.find("op");

        if (a_it == args.end() || b_it == args.end() || op_it == args.end()) {
            return std::string("[ERROR] Missing arguments for calculate tool");
        }

        double a = std::stod(a_it->second);
        double b = std::stod(b_it->second);
        std::string op = op_it->second;

        double result = 0.0;
        if (op == "+") result = a + b;
        else if (op == "-") result = a - b;
        else if (op == "*") result = a * b;
        else if (op == "/") result = (b != 0.0) ? a / b : std::numeric_limits<double>::quiet_NaN();

        return result;
    });

    return true;
}

bool ToolRegistry::init_default_tools = ToolRegistry::register_default_tools();

} // namespace agenticdsl
```

### 5. LLM 适配器实现 (`src/llm/llama_adapter.cpp`)

```c++
#include "agenticdsl/llm/llama_adapter.h"
#include <stdexcept>
#include <thread>
#include <chrono>
#include <iostream>

namespace agenticdsl {

LlamaAdapter::LlamaAdapter(const Config& config) : config_(config) {
    load_model();
}

LlamaAdapter::~LlamaAdapter() {
    if (ctx_) {
        llama_free(ctx_);
    }
    if (model_) {
        llama_free_model(model_);
    }
}

void LlamaAdapter::load_model() {
    // 初始化llama参数
    llama_model_params model_params = llama_model_default_params();
    model_params.n_ctx = config_.n_ctx;

    // 加载模型
    model_ = llama_load_model_from_file(config_.model_path.c_str(), model_params);
    if (!model_) {
        throw std::runtime_error("Failed to load model: " + config_.model_path);
    }

    // 创建上下文
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = config_.n_ctx;
    ctx_params.n_threads = config_.n_threads;
    ctx_params.n_threads_batch = config_.n_threads;

    ctx_ = llama_new_context_with_model(model_, ctx_params);
    if (!ctx_) {
        llama_free_model(model_);
        throw std::runtime_error("Failed to create context");
    }
}

std::string LlamaAdapter::generate(const std::string& prompt) {
    if (!is_loaded()) {
        return "[ERROR] Model not loaded";
    }

    // 简化的生成逻辑
    // 实际实现会更复杂，包括tokenization, generation loop等
    std::vector<llama_token> tokens_list;
    tokens_list.resize(prompt.size() + 256); // 预分配空间

    // Tokenize输入
    int n_prompt_tokens = llama_tokenize(model_, prompt.c_str(), tokens_list.data(),
                                        static_cast<int>(tokens_list.size()), true);

    if (n_prompt_tokens < 0) {
        return "[ERROR] Failed to tokenize prompt";
    }

    // 设置KV cache
    llama_set_rng_seed(ctx_, 1234); // 设置随机种子

    // 推理
    for (int i = 0; i < n_prompt_tokens - 1; i++) {
        llama_decode(ctx_, llama_batch_get_one(&tokens_list[i], 1, i, 0));
    }

    // 生成响应
    std::string response;
    for (int i = 0; i < config_.n_predict; i++) {
        llama_token id = 0;

        // 获取最后一个token的logits
        auto logits = llama_get_logits_ith(ctx_, n_prompt_tokens - 1 + i);
        auto n_vocab = llama_n_vocab(model_);

        // 采样下一个token (简化版)
        id = llama_sample_token_greedy(ctx_, logits);

        if (id == llama_token_eos(model_)) {
            break;
        }

        // 解码token
        char buf[8] = {0};
        llama_token_to_piece(model_, id, buf, sizeof(buf), 0, true);
        response += buf;

        // 解码下一个token
        llama_decode(ctx_, llama_batch_get_one(&id, 1, n_prompt_tokens + i, 0));
    }

    return response;
}

std::vector<std::string> LlamaAdapter::generate_batch(const std::vector<std::string>& prompts) {
    std::vector<std::string> results;
    results.reserve(prompts.size());

    for (const auto& prompt : prompts) {
        results.push_back(generate(prompt));
    }

    return results;
}

bool LlamaAdapter::is_loaded() const {
    return model_ != nullptr && ctx_ != nullptr;
}

std::string LlamaAdapter::get_context() {
    if (!is_loaded()) {
        return "[ERROR] Context not available, model not loaded";
    }
    // This is a simplified representation, llama.cpp doesn't provide a direct way
    // to dump the full context as a string. This is just a placeholder.
    return "[CONTEXT DUMP - IMPLEMENTATION DEPENDENT]";
}

} // namespace agenticdsl
```

### 6. 解析器实现 (`src/core/parser.cpp`)

```c++
#include "agenticdsl/core/parser.h"
#include "agenticdsl/core/nodes.h"
#include "agenticdsl/dsl/spec.h"
#include "common/utils.h" // v1.1: New include
#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace agenticdsl {

std::vector<ParsedGraph> MarkdownParser::parse_from_string(const std::string& markdown_content) {
    std::vector<ParsedGraph> graphs;

    auto pathed_blocks = extract_pathed_blocks(markdown_content); // v1.1: Use new utility
    for (auto& [path, yaml_content] : pathed_blocks) {
        try {
            nlohmann::json json_doc = nlohmann::json::parse(yaml_content);

            // Check if it's a subgraph definition (v1.1)
            if (json_doc.contains("graph_type") && json_doc["graph_type"] == "subgraph") {
                 ParsedGraph graph;
                 graph.path = path;
                 if (json_doc.contains("metadata")) {
                     graph.metadata = json_doc["metadata"];
                 }
                 // Subgraph nodes are handled differently or ignored in v1
                 // For simplicity, we might parse the 'entry' node and its direct successors here if needed
                 // For now, let's focus on top-level nodes
                 continue; // Skip subgraph definitions for now
            }

            // Assume it's a node definition
            if (json_doc.contains("type")) {
                auto node = create_node_from_json(path, json_doc);
                if (node) {
                    ParsedGraph graph;
                    graph.nodes.push_back(std::move(node));
                    graph.path = path;
                    if (json_doc.contains("metadata")) {
                        graph.metadata = json_doc["metadata"];
                    }
                    graphs.push_back(std::move(graph));
                }
            }
        } catch (const nlohmann::json::parse_error& e) {
            // If parsing fails, skip this block
            continue;
        } catch (const std::exception& e) {
            // Log or handle other errors during node creation
            continue;
        }
    }

    return graphs;
}

std::vector<ParsedGraph> MarkdownParser::parse_from_file(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + file_path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    return parse_from_string(content);
}

std::unique_ptr<Node> MarkdownParser::create_node_from_json(const NodePath& path, const nlohmann::json& node_json) {
    std::string type_str = node_json["type"];

    std::vector<NodePath> next_paths;
    if (node_json.contains("next")) {
        if (node_json["next"].is_string()) {
            next_paths.push_back(node_json["next"]);
        } else if (node_json["next"].is_array()) {
            for (auto& next_path : node_json["next"]) {
                next_paths.push_back(next_path.get<std::string>());
            }
        }
    }

    nlohmann::json metadata = nlohmann::json::object();
    if (node_json.contains("metadata") && node_json["metadata"].is_object()) {
        metadata = node_json["metadata"];
    }

    if (type_str == "start") {
        auto node = std::make_unique<StartNode>(std::move(path), std::move(next_paths));
        node->metadata = metadata;
        return std::move(node);
    } else if (type_str == "end") {
        auto node = std::make_unique<EndNode>(std::move(path));
        node->metadata = metadata;
        return std::move(node);
    } else if (type_str == "assign") { // v1.1: renamed from set
        std::unordered_map<std::string, std::string> assign;
        if (node_json.contains("assign") && node_json["assign"].is_object()) {
            for (auto& [key, value] : node_json["assign"].items()) {
                assign[key] = value.get<std::string>();
            }
        }
        auto node = std::make_unique<AssignNode>(std::move(path), std::move(assign), std::move(next_paths));
        node->metadata = metadata;
        return std::move(node);
    } else if (type_str == "llm_call") {
        std::string prompt = node_json["prompt_template"];
        std::vector<std::string> output_keys;
        if (node_json["output_keys"].is_string()) {
            output_keys.push_back(node_json["output_keys"]);
        } else if (node_json["output_keys"].is_array()) {
            for (auto& key : node_json["output_keys"]) {
                output_keys.push_back(key.get<std::string>());
            }
        } else {
            // Handle case where output_keys is missing or not a string/array
            // For v1 compatibility, assume a default key or throw error
            throw std::runtime_error("Invalid or missing output_keys for llm_call node: " + path);
        }
        auto node = std::make_unique<LLMCallNode>(std::move(path), std::move(prompt), std::move(output_keys), std::move(next_paths));
        node->metadata = metadata;
        return std::move(node);
    } else if (type_str == "tool_call") {
        std::string tool = node_json["tool"];
        std::string output_key_str = node_json["output_keys"]; // v1.1: renamed from output_key
        std::vector<std::string> output_keys;
        if (node_json["output_keys"].is_string()) {
            output_keys.push_back(node_json["output_keys"]);
        } else if (node_json["output_keys"].is_array()) {
            for (auto& key : node_json["output_keys"]) {
                output_keys.push_back(key.get<std::string>());
            }
        } else {
            throw std::runtime_error("Invalid or missing output_keys for tool_call node: " + path);
        }
        std::unordered_map<std::string, std::string> args;
        if (node_json.contains("arguments") && node_json["arguments"].is_object()) { // v1.1: renamed from args
            for (auto& [key, value] : node_json["arguments"].items()) {
                args[key] = value.get<std::string>();
            }
        }
        auto node = std::make_unique<ToolCallNode>(std::move(path), std::move(tool), std::move(args), std::move(output_keys), std::move(next_paths));
        node->metadata = metadata;
        return std::move(node);
    } else if (type_str == "resource") { // v1.1: new node type
        ResourceType resource_type;
        std::string type_str_lower = node_json["resource_type"];
        if (type_str_lower == "file") resource_type = ResourceType::FILE;
        else if (type_str_lower == "postgres") resource_type = ResourceType::POSTGRES;
        else if (type_str_lower == "mysql") resource_type = ResourceType::MYSQL;
        else if (type_str_lower == "sqlite") resource_type = ResourceType::SQLITE;
        else if (type_str_lower == "api_endpoint") resource_type = ResourceType::API_ENDPOINT;
        else if (type_str_lower == "vector_store") resource_type = ResourceType::VECTOR_STORE;
        else resource_type = ResourceType::CUSTOM; // Fallback

        std::string uri = node_json["uri"];
        std::string scope = node_json.value("scope", "global"); // Default to global

        auto node = std::make_unique<ResourceNode>(std::move(path), resource_type, std::move(uri), std::move(scope));
        node->metadata = metadata;
        return std::move(node);
    }

    return nullptr; // Unknown type
}

void MarkdownParser::validate_nodes(const std::vector<std::unique_ptr<Node>>& nodes) {
    // Implement validation logic if needed
    // For now, rely on DSLValidator
}

} // namespace agenticdsl
```

### 7. 执行器实现 (`src/core/executor.cpp`)

```c++
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
```

### 8. 主引擎实现 (`src/core/engine.cpp`)

```c++
#include "agenticdsl/core/engine.h"
#include <fstream>
#include <sstream>

namespace agenticdsl {

std::unique_ptr<AgenticDSLEngine> AgenticDSLEngine::from_markdown(const std::string& markdown_content,
                                                                const Context& initial_context) {
    MarkdownParser parser;
    auto graphs = parser.parse_from_string(markdown_content);

    if (graphs.empty()) {
        throw std::runtime_error("No valid graphs found in markdown content");
    }

    // v1.1: For simplicity, combine all parsed nodes into one executor.
    // In a more complex system, you might handle subgraphs differently.
    std::vector<std::unique_ptr<Node>> all_nodes;
    for (auto& graph : graphs) {
        all_nodes.insert(all_nodes.end(), std::make_move_iterator(graph.nodes.begin()), std::make_move_iterator(graph.nodes.end()));
    }

    auto engine = std::make_unique<AgenticDSLEngine>(std::move(all_nodes));

    // 初始化LLM适配器
    LlamaAdapter::Config config;
    config.model_path = "models/qwen-0.6b.gguf"; // 替换为实际模型路径
    config.n_ctx = 2048;
    config.n_threads = std::thread::hardware_concurrency();
    engine->llama_adapter_ = std::make_unique<LlamaAdapter>(config);

    return engine;
}

std::unique_ptr<AgenticDSLEngine> AgenticDSLEngine::from_file(const std::string& file_path,
                                                             const Context& initial_context) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + file_path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    return from_markdown(content, initial_context);
}

ExecutionResult AgenticDSLEngine::run(const Context& context) {
    if (!executor_) {
        throw std::runtime_error("Executor not initialized");
    }
    return executor_->execute(context);
}

AgenticDSLEngine::AgenticDSLEngine(std::vector<std::unique_ptr<Node>> nodes)
    : executor_(std::make_unique<ModernFlowExecutor>(std::move(nodes))) {}

} // namespace agenticdsl
```

### 9. DSL规范实现 (`src/dsl/spec.cpp`)

```c++
#include "agenticdsl/dsl/spec.h"
#include <stdexcept>
#include <regex>

namespace agenticdsl {

bool DSLValidator::validate_nodes(const std::vector<nlohmann::json>& nodes_json) {
    if (nodes_json.empty()) {
        throw std::runtime_error("Nodes list cannot be empty");
    }

    // Check for unique paths
    std::unordered_set<std::string> seen_paths;
    for (const auto& node : nodes_json) {
        if (!node.contains("path") || !node["path"].is_string()) {
            throw std::runtime_error("Node missing required 'path' field or 'path' is not a string");
        }
        std::string path = node["path"];
        if (!validate_path(path)) {
            throw std::runtime_error("Invalid node path format: " + path);
        }
        if (seen_paths.count(path)) {
            throw std::runtime_error("Duplicate node path found: " + path);
        }
        seen_paths.insert(path);
    }

    // Validate each node
    for (const auto& node : nodes_json) {
        if (!validate_node_type(node)) {
            return false;
        }
    }

    // Validate graph structure
    if (!validate_graph_structure(nodes_json)) {
        return false;
    }

    return true;
}

bool DSLValidator::validate_path(const std::string& path) {
    // Must start with /
    if (path.empty() || path[0] != '/') {
        return false;
    }
    // Must only contain allowed characters
    std::regex path_regex(R"(^/[\w/\-]*$)");
    return std::regex_match(path, path_regex);
}

bool DSLValidator::validate_node_type(const nlohmann::json& node_json) {
    if (!node_json.contains("type") || !node_json["type"].is_string()) {
        return false;
    }

    std::string type = node_json["type"];
    if (type == "start") return validate_start_node(node_json);
    if (type == "end") return validate_end_node(node_json);
    if (type == "assign") return validate_assign_node(node_json); // v1.1: renamed
    if (type == "llm_call") return validate_llm_call_node(node_json);
    if (type == "tool_call") return validate_tool_call_node(node_json);
    if (type == "resource") return validate_resource_node(node_json); // v1.1: new

    return false; // Unknown type
}

bool DSLValidator::validate_graph_structure(const std::vector<nlohmann::json>& nodes_json) {
    std::unordered_map<std::string, const nlohmann::json*> node_map;
    for (const auto& node : nodes_json) {
        if (node.contains("path")) {
            node_map[node["path"]] = &node;
        }
    }

    // Find start node
    int start_count = 0;
    for (const auto& node : nodes_json) {
        if (node["type"] == "start") {
            start_count++;
        }
    }
    if (start_count != 1) {
        throw std::runtime_error("Must have exactly one start node");
    }

    // Check next references
    for (const auto& node : nodes_json) {
        if (node.contains("next")) {
            if (node["next"].is_string()) {
                std::string next_id = node["next"];
                if (node_map.find(next_id) == node_map.end()) {
                    throw std::runtime_error("Node '" + node["path"].get<std::string>() + "' references non-existent next node: " + next_id);
                }
            } else if (node["next"].is_array()) {
                for (auto& next_id_val : node["next"]) {
                    std::string next_id = next_id_val.get<std::string>();
                    if (node_map.find(next_id) == node_map.end()) {
                        throw std::runtime_error("Node '" + node["path"].get<std::string>() + "' references non-existent next node: " + next_id);
                    }
                }
            }
        }
    }

    // Simple cycle detection for linear flow (v1)
    std::unordered_set<std::string> visited;
    std::string current_id = "";
    for (const auto& node : nodes_json) {
        if (node["type"] == "start") {
            current_id = node["path"];
            break;
        }
    }

    while (!current_id.empty() && !visited.count(current_id)) {
        visited.insert(current_id);
        auto it = node_map.find(current_id);
        if (it == node_map.end()) break;
        const auto& current_node = *(it->second);
        if (current_node.contains("next")) {
            if (current_node["next"].is_string()) {
                current_id = current_node["next"];
            } else if (current_node["next"].is_array() && !current_node["next"].empty()) {
                current_id = current_node["next"][0]; // v1: take first
            } else {
                current_id = "";
            }
        } else {
            current_id = "";
        }
    }

    if (!current_id.empty() && visited.count(current_id)) {
        throw std::runtime_error("Cycle detected in graph");
    }

    return true;
}

bool DSLValidator::validate_start_node(const nlohmann::json& node_json) {
    if (!node_json.contains("path") || !node_json.contains("next")) {
        return false;
    }
    return node_json["path"].is_string() && (node_json["next"].is_string() || node_json["next"].is_array());
}

bool DSLValidator::validate_end_node(const nlohmann::json& node_json) {
    if (!node_json.contains("path")) {
        return false;
    }
    return node_json["path"].is_string();
}

bool DSLValidator::validate_assign_node(const nlohmann::json& node_json) { // v1.1: renamed
    if (!node_json.contains("path") || !node_json.contains("assign") || !node_json.contains("next")) {
        return false;
    }
    return node_json["path"].is_string() &&
           node_json["assign"].is_object() &&
           (node_json["next"].is_string() || node_json["next"].is_array());
}

bool DSLValidator::validate_llm_call_node(const nlohmann::json& node_json) {
    if (!node_json.contains("path") || !node_json.contains("prompt_template") || !node_json.contains("output_keys") || !node_json.contains("next")) {
        return false;
    }
    return node_json["path"].is_string() &&
           node_json["prompt_template"].is_string() &&
           (node_json["output_keys"].is_string() || node_json["output_keys"].is_array()) && // v1.1: allow array
           (node_json["next"].is_string() || node_json["next"].is_array());
}

bool DSLValidator::validate_tool_call_node(const nlohmann::json& node_json) {
    if (!node_json.contains("path") || !node_json.contains("tool") || !node_json.contains("arguments") || !node_json.contains("output_keys") || !node_json.contains("next")) { // v1.1: renamed args to arguments
        return false;
    }
    return node_json["path"].is_string() &&
           node_json["tool"].is_string() &&
           node_json["arguments"].is_object() && // v1.1: renamed
           (node_json["output_keys"].is_string() || node_json["output_keys"].is_array()) && // v1.1: allow array
           (node_json["next"].is_string() || node_json["next"].is_array());
}

bool DSLValidator::validate_resource_node(const nlohmann::json& node_json) { // v1.1: new
    if (!node_json.contains("path") || !node_json.contains("resource_type") || !node_json.contains("uri")) {
        return false;
    }
    return node_json["path"].is_string() &&
           node_json["resource_type"].is_string() &&
           node_json["uri"].is_string();
}

} // namespace agenticdsl
```

### 10. 示例：v1.1 Agent 循环 (`examples/agent_loop_v11_example.cpp`)

```c++
#include "agenticdsl/core/engine.h"
#include "common/utils.h"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    // 1. 初始化 LLM 适配器 (假设模型路径正确)
    agenticdsl::LlamaAdapter::Config llm_config;
    llm_config.model_path = "models/qwen-0.6b.gguf"; // 替换为实际模型路径
    llm_config.n_ctx = 2048;
    llm_config.n_threads = std::thread::hardware_concurrency();
    agenticdsl::LlamaAdapter llm_adapter(llm_config);

    if (!llm_adapter.is_loaded()) {
        std::cerr << "Failed to load LLM model. Please check the path: " << llm_config.model_path << std::endl;
        return 1;
    }

    std::cout << "LLM Model loaded successfully.\n";

    // 2. 初始化上下文
    agenticdsl::Context agent_context;
    agent_context["task"] = "Calculate 15 + 27 and then get the weather in Beijing.";
    agent_context["history"] = nlohmann::json::array(); // 记录执行历史

    // v1.1: Example prompt for LLM to generate AgenticDSL v1.1 format
    std::string agent_prompt = R"(
You are an AI assistant. Your task is to generate AgenticDSL code to fulfill the user's request.
The current context is: {{ task }}

Here is the history of executed DSL code and results:
{% for item in history %}
- DSL: {{ item.dsl_code }}
- Result: {{ item.result }}
{% endfor %}

Generate the next AgenticDSL code block to execute. Follow the AgenticDSL v1.1 specification:
1. Each node starts with `### AgenticDSL '/path'`
2. Use `/main/step1`, `/main/step2`, etc. for your paths
3. Use `assign` instead of `set`
4. Use `arguments` instead of `args`
5. Use `output_keys` (can be a string or list) instead of `output_key`
6. Use Inja syntax for all dynamic content: `{{ variable }}`, `{% if condition %}...{% endif %}`, etc.
7. If you need to reference external resources, first define a `resource` node like:
   ```markdown
   ### AgenticDSL `/resources/weather_cache`
   ```yaml
   # --- BEGIN AgenticDSL ---
   type: resource
   resource_type: file
   uri: "/tmp/weather_cache.json"
   scope: global
   # --- END AgenticDSL ---
   ```
   ```
   Then reference it in other nodes like `{{ resources.weather_cache.uri }}`.
   Remember: Do NOT try to modify `resources.xxx` from within a node. Only read from it.
   Use a dedicated tool for writing if needed.

The DSL should follow this format:

### AgenticDSL `/main/step1`
```yaml
# --- BEGIN AgenticDSL ---
type: start
next: ["/main/step2"]
# --- END AgenticDSL ---
```

### AgenticDSL `/main/step2`
```yaml
# --- BEGIN AgenticDSL ---
type: assign
assign:
  location: "{{ user_input }}"
  query: "weather in {{ location }}"
output_keys: ["loc", "qry"]
next: ["/main/step3"]
# --- END AgenticDSL ---
```

)";

    // 3. Agent 循环 (Simplified for v1.1)
    for (int step = 0; step < 2; ++step) { // 最多执行2步
        std::cout << "\n--- Agent Step " << (step + 1) << " ---\n";

        // a. 让 LLM 生成 DSL 代码 (v1.1 format)
        std::string prompt = agenticdsl::InjaTemplateRenderer::render(agent_prompt, agent_context);
        std::cout << "Prompt sent to LLM:\n" << prompt << "\n\n";

        std::string generated_dsl = llm_adapter.generate(prompt);
        std::cout << "LLM Generated DSL:\n" << generated_dsl << "\n";

        // b. 提取 AgenticDSL v1.1 blocks (简化处理，实际需要更复杂的解析)
        // Here we assume the LLM generates the correct format as per prompt
        // Use the utility function to extract blocks
        auto blocks = agenticdsl::extract_pathed_blocks(generated_dsl);
        std::string aggregated_dsl_content = "# AgenticDSL v1.1 Generated Flow\n";
        for (const auto& [path, yaml_content] : blocks) {
             aggregated_dsl_content += "### AgenticDSL `" + path + "`\n```yaml\n# --- BEGIN AgenticDSL ---\n" + yaml_content + "\n# --- END AgenticDSL ---\n```\n";
        }

        // c. 创建并执行引擎
        try {
            auto engine = agenticdsl::AgenticDSLEngine::from_markdown(aggregated_dsl_content, agent_context);
            auto result = engine->run(agent_context);

            // e. 更新上下文和历史记录
            agent_context = result.final_context;
            nlohmann::json history_entry;
            history_entry["dsl_code"] = aggregated_dsl_content;
            history_entry["result"] = result.success ? result.final_context.dump() : result.message;
            agent_context["history"].push_back(history_entry);

            std::cout << "\nExecution Result:\n";
            if (result.success) {
                std::cout << "Success!\nFinal Context: " << result.final_context.dump(2) << "\n";
            } else {
                std::cout << "Failed: " << result.message << "\n";
            }

        } catch (const std::exception& e) {
            std::cout << "Error executing generated DSL: " << e.what() << "\n";
            nlohmann::json history_entry;
            history_entry["dsl_code"] = aggregated_dsl_content;
            history_entry["result"] = std::string("[ERROR] ") + e.what();
            agent_context["history"].push_back(history_entry);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 短暂延迟
    }

    std::cout << "\n--- Final Agent Context ---\n" << agent_context.dump(2) << std::endl;

    return 0;
}
```

### 11. CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(AgenticDSL VERSION 1.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# llama.cpp
add_subdirectory(llama.cpp)
set(LLAMA_LIB llama)

# nlohmann/json
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# inja
FetchContent_Declare(
    inja
    GIT_REPOSITORY https://github.com/pantor/inja.git
    GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(inja)

# 包含目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 源文件
file(GLOB_RECURSE SOURCES "src/*.cpp")

# 创建库
add_library(agenticdsl ${SOURCES})
target_link_libraries(agenticdsl
    ${LLAMA_LIB}
    nlohmann_json::nlohmann_json
    inja
    Threads::Threads
)

target_include_directories(agenticdsl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(agenticdsl PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-O3 -march=native -mtune=native>
    $<$<CXX_COMPILER_ID:MSVC>:/O2>
)

# 示例程序
add_executable(agent_loop_v11_example examples/agent_loop_v11_example.cpp)
target_link_libraries(agent_loop_v11_example agenticdsl)

# 测试
enable_testing()
add_executable(test_v11_basic tests/test_v11_basic.cpp)
target_link_libraries(test_v11_basic agenticdsl)
add_test(NAME test_v11_basic COMMAND test_v11_basic)

install(TARGETS agenticdsl
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/ DESTINATION include)
```

这个完整的 v1.1 重构项目包含了所有规范中的关键特性：路径化块、`assign`/`arguments`/`output_keys` 命名、Inja 安全模式、资源节点、以及统一的执行/生成格式。
