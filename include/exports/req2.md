    
---
# 项目文件组织结构

*此目录结构列出了本次导出包含的所有文件*

- agenticdsl    
    - core    
        - `engine.h`    
        - `executor.h`    
        - `nodes.h`    
        - `parser.h`    
    - dsl    
        - `spec.h`    
        - `templates.h`    
    - llm    
        - `llama_adapter.h`    
    - resources    
        - `manager.h`    
    - tools    
        - `registry.h`    
- common    
    - `types.h`    
    - `utils.h`    
- src    
    - core    
        - `engine.cpp`    
        - `executor.cpp`    
        - `nodes.cpp`    
        - `parser.cpp`    
    - dsl    
        - `spec.cpp`    
        - `templates.cpp`    
    - llm    
        - `llama_adapter.cpp`    
    - resources    
        - `manager.cpp`    
    - tools    
        - `registry.cpp`    
    
    
## `common/utils.h`
    
```cpp
#ifndef AGENTICDSL_COMMON_UTILS_H
#define AGENTICDSL_COMMON_UTILS_H

#include "types.h"
#include <string>
#include <vector>
#include <regex>
#include <nlohmann/json.hpp>

namespace agenticdsl {

inline std::vector<std::pair<NodePath, std::string>> extract_pathed_blocks(const std::string& markdown_content) {
    std::vector<std::pair<NodePath, std::string>> blocks;

    // Use [\s\S] instead of . to match any character including newlines
    std::regex block_pattern(
        R"(###\s+AgenticDSL\s+`(/[\w/\-]+)`\s*\n"
        R"(# --- BEGIN AgenticDSL ---\s*\n([\s\S]*?)\n"
        R"(# --- END AgenticDSL ---))"
    );

    std::sregex_iterator iter(markdown_content.begin(), markdown_content.end(), block_pattern);
    std::sregex_iterator end;

    for (; iter != end; ++iter) {
        std::string path = (*iter)[1].str();
        std::string yaml_content = (*iter)[2].str();
        blocks.emplace_back(std::move(path), std::move(yaml_content));
    }

    return blocks;
}

// Validate node path format: must start with / and contain only allowed chars
inline bool is_valid_node_path(const std::string& path) {
    if (path.empty() || path[0] != '/') return false;
    std::regex valid(R"(^/[\w/\-]+$)");
    return std::regex_match(path, valid);
}

// Parse output_keys (string or array) from JSON
inline std::vector<std::string> parse_output_keys(const nlohmann::json& node_json, const NodePath& path) {
    if (!node_json.contains("output_keys")) {
        throw std::runtime_error("Missing 'output_keys' in node: " + path);
    }
    const auto& ok = node_json["output_keys"];
    if (ok.is_string()) {
        return {ok.get<std::string>()};
    } else if (ok.is_array()) {
        std::vector<std::string> keys;
        for (const auto& k : ok) {
            keys.push_back(k.get<std::string>());
        }
        return keys;
    } else {
        throw std::runtime_error("'output_keys' must be string or array in node: " + path);
    }
}

// Parse ResourceType from string
inline ResourceType parse_resource_type(const std::string& type_str) {
    if (type_str == "file") return ResourceType::FILE;
    if (type_str == "postgres") return ResourceType::POSTGRES;
    if (type_str == "mysql") return ResourceType::MYSQL;
    if (type_str == "sqlite") return ResourceType::SQLITE;
    if (type_str == "api_endpoint") return ResourceType::API_ENDPOINT;
    if (type_str == "vector_store") return ResourceType::VECTOR_STORE;
    if (type_str == "custom") return ResourceType::CUSTOM;
    throw std::runtime_error("Unknown resource_type '" + type_str + "'");
}

} // namespace agenticdsl

#endif // AGENTICDSL_COMMON_UTILS_H

```
    
## `common/types.h`
    
```cpp
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
    std::optional<NodePath> paused_at; // set if paused at llm_call
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
    
## `agenticdsl/tools/registry.h`
    
```cpp
#ifndef AGENTICDSL_TOOLS_REGISTRY_H
#define AGENTICDSL_TOOLS_REGISTRY_H

#include "common/types.h"
#include <unordered_map>
#include <functional>
#include <string>
#include <vector>

namespace agenticdsl {

class ToolRegistry {
public:
    static ToolRegistry& instance();

    template<typename Func>
    void register_tool(std::string name, Func&& func);

    bool has_tool(const std::string& name) const;
    nlohmann::json call_tool(const std::string& name, const std::unordered_map<std::string, std::string>& args);
    std::vector<std::string> list_tools() const;

private:
    ToolRegistry() = default;
    void register_default_tools();

    std::unordered_map<std::string, std::function<nlohmann::json(const std::unordered_map<std::string, std::string>&)>> tools_;
};

} // namespace agenticdsl

#endif

```
    
## `agenticdsl/llm/llama_adapter.h`
    
```cpp
#ifndef AGENTICDSL_LLM_LLAMA_ADAPTER_H
#define AGENTICDSL_LLM_LLAMA_ADAPTER_H

#include "common/types.h"
#include <string>
#include <memory>
#include <vector>
#include <llama.h>

namespace agenticdsl {

class LlamaAdapter {
public:
    struct Config {
        std::string model_path;
        int n_ctx = 2048;
        int n_threads = 4;
        float temperature = 0.7f;
        float min_p = 0.05f;
        int n_predict = 512;
    };

    explicit LlamaAdapter(const Config& config);
    ~LlamaAdapter();

    std::string generate(const std::string& prompt);
    bool is_loaded() const;

private:
    Config config_;
    std::unique_ptr<llama_model, decltype(&llama_model_free)> model_;
    std::unique_ptr<llama_context, decltype(&llama_free)> ctx_;
    std::unique_ptr<llama_sampler, decltype(&llama_sampler_free)> sampler_;

    std::vector<llama_token> tokenize(const std::string& text, bool add_bos);
    std::string detokenize(llama_token token);
};

} // namespace agenticdsl

#endif

```
    
## `agenticdsl/core/engine.h`
    
```cpp
#ifndef AGENTICDSL_CORE_ENGINE_H
#define AGENTICDSL_CORE_ENGINE_H

#include "agenticdsl/core/executor.h"
#include "agenticdsl/core/parser.h"
#include "agenticdsl/llm/llama_adapter.h"
#include "agenticdsl/tools/registry.h"
#include <memory>
#include <string>

namespace agenticdsl {

class AgenticDSLEngine {
public:
    static std::unique_ptr<AgenticDSLEngine> from_markdown(const std::string& markdown_content);
    static std::unique_ptr<AgenticDSLEngine> from_file(const std::string& file_path);

    ExecutionResult run(const Context& context = Context{});
    void append_graphs(const std::vector<ParsedGraph>& new_graphs);

    template<typename Func>
    void register_tool(std::string_view name, Func&& func) {
        ToolRegistry::instance().register_tool(name, std::forward<Func>(func));
    }

    LlamaAdapter* get_llm_adapter() { return llama_adapter_.get(); }

    AgenticDSLEngine(std::vector<ParsedGraph> initial_graphs);
private:

    std::vector<ParsedGraph> full_graphs_;
    std::unique_ptr<ModernFlowExecutor> executor_;
    std::unique_ptr<LlamaAdapter> llama_adapter_;
};

} // namespace agenticdsl

#endif

```
    
## `agenticdsl/core/parser.h`
    
```cpp
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
    std::unique_ptr<Node> create_node_from_json(const NodePath& path, const nlohmann::json& node_json);
    void validate_nodes(const std::vector<std::unique_ptr<Node>>& nodes);
};

} // namespace agenticdsl

#endif

```
    
## `agenticdsl/core/nodes.h`
    
```cpp
#ifndef AGENICDSL_CORE_NODES_H
#define AGENICDSL_CORE_NODES_H

#include "common/types.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace agenticdsl {

// Forward declarations
class InjaTemplateRenderer;

// Base Node
struct Node {
    NodePath path;
    NodeType type;
    std::vector<NodePath> next;
    nlohmann::json metadata;

    Node(NodePath path,
         NodeType type,
         std::vector<NodePath> next = {},
         nlohmann::json metadata = nlohmann::json::object())
        : path(std::move(path)),
          type(type),
          next(std::move(next)),
          metadata(std::move(metadata)) {}

    virtual ~Node() = default;
    [[nodiscard]] virtual Context execute(Context& context) = 0;
};

// Start Node
struct StartNode : public Node {
    StartNode(NodePath path, std::vector<NodePath> next_paths = {});
    [[nodiscard]] Context execute(Context& context) override;
};

// End Node
struct EndNode : public Node {
    EndNode(NodePath path);
    [[nodiscard]] Context execute(Context& context) override;
};

// Assign Node (v1.1: renamed from Set)
struct AssignNode : public Node {
    std::unordered_map<std::string, std::string> assign;

    AssignNode(NodePath path,
               std::unordered_map<std::string, std::string> assigns,
               std::vector<NodePath> next_paths = {});
    [[nodiscard]] Context execute(Context& context) override;
};

// LLM Call Node
struct LLMCallNode : public Node {
    std::string prompt_template;
    std::vector<std::string> output_keys;

    LLMCallNode(NodePath path,
                std::string prompt,
                std::vector<std::string> output_keys,
                std::vector<NodePath> next_paths = {});
    [[nodiscard]] Context execute(Context& context) override;
};

// Tool Call Node
struct ToolCallNode : public Node {
    std::string tool_name;
    std::unordered_map<std::string, std::string> arguments;
    std::vector<std::string> output_keys;

    ToolCallNode(NodePath path,
                 std::string tool_name,
                 std::unordered_map<std::string, std::string> arguments,
                 std::vector<std::string> output_keys,
                 std::vector<NodePath> next_paths = {});
    [[nodiscard]] Context execute(Context& context) override;
};

// Resource Node (v1.1)
struct ResourceNode : public Node {
    ResourceType resource_type;
    std::string uri;
    std::string scope;

    ResourceNode(NodePath path,
                 ResourceType type,
                 std::string uri,
                 std::string scope = "global",
                 nlohmann::json metadata = nlohmann::json::object());
    [[nodiscard]] Context execute(Context& context) override;
};

} // namespace agenticdsl

#endif // AGENICDSL_CORE_NODES_H

```
    
## `agenticdsl/core/executor.h`
    
```cpp
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

```
    
## `agenticdsl/resources/manager.h`
    
```cpp
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
    
## `agenticdsl/dsl/spec.h`
    
```cpp
#ifndef AGENTICDSL_DSL_SPEC_H
#define AGENTICDSL_DSL_SPEC_H

#include <nlohmann/json.hpp>
#include <string>

namespace agenticdsl {

/**
 * 注意：不校验 path（路径由解析器提供），不校验 next 引用存在性（图级校验）
 */
class NodeValidator {
public:
    static void validate(const nlohmann::json& node_json);
    static void validate_type(const std::string& type);

private:
    static void validate_start(const nlohmann::json& node);
    static void validate_end(const nlohmann::json& node);
    static void validate_assign(const nlohmann::json& node);
    static void validate_llm_call(const nlohmann::json& node);
    static void validate_tool_call(const nlohmann::json& node);
    static void validate_resource(const nlohmann::json& node);
    static void validate_output_keys(const nlohmann::json& node);
    static void validate_next(const nlohmann::json& node);
};

} // namespace agenticdsl

#endif // AGENTICDSL_DSL_SPEC_H

```
    
## `agenticdsl/dsl/templates.h`
    
```cpp
#ifndef AGENTICDSL_DSL_TEMPLATES_H
#define AGENTICDSL_DSL_TEMPLATES_H

#include "common/types.h"
#include <inja/inja.hpp>
#include <string>
#include <string_view>

namespace agenticdsl {

class InjaTemplateRenderer {
public:
    InjaTemplateRenderer();

    static std::string render(std::string_view template_str, const Context& context);

    std::string render_with_env(std::string_view template_str, const Context& context);

    // Remove explicit template instantiations — not needed in v3

private:
    inja::Environment env_;
    void configure_security();
};

} // namespace agenticdsl

#endif

```
    
## `src/tools/registry.cpp`
    
```cpp
#include "agenticdsl/tools/registry.h"
#include <stdexcept>
#include <limits>

namespace agenticdsl {

ToolRegistry& ToolRegistry::instance() {
    static ToolRegistry registry;
    static bool initialized = false;
    if (!initialized) {
        registry.register_default_tools();
        initialized = true;
    }
    return registry;
}

void ToolRegistry::register_default_tools() {
    register_tool("web_search", [](const std::unordered_map<std::string, std::string>& args) -> nlohmann::json {
        auto it = args.find("query");
        std::string query = (it != args.end()) ? it->second : "default query";
        return nlohmann::json{{"results", "[MOCK] Search results for: " + query}};
    });

    register_tool("get_weather", [](const std::unordered_map<std::string, std::string>& args) -> nlohmann::json {
        auto it = args.find("location");
        std::string loc = (it != args.end()) ? it->second : "unknown";
        return nlohmann::json{
            {"location", loc},
            {"condition", "Sunny"},
            {"temperature_c", 22}
        };
    });

    register_tool("calculate", [](const std::unordered_map<std::string, std::string>& args) -> nlohmann::json {
        auto a_it = args.find("a");
        auto b_it = args.find("b");
        auto op_it = args.find("op");

        if (a_it == args.end() || b_it == args.end() || op_it == args.end()) {
            return nlohmann::json{{"error", "Missing arguments: a, b, op"}};
        }

        try {
            double a = std::stod(a_it->second);
            double b = std::stod(b_it->second);
            std::string op = op_it->second;

            if (op == "/" && b == 0.0) {
                return nlohmann::json{{"error", "Division by zero"}};
            }

            double result = 0.0;
            if (op == "+") result = a + b;
            else if (op == "-") result = a - b;
            else if (op == "*") result = a * b;
            else if (op == "/") result = a / b;
            else return nlohmann::json{{"error", "Unsupported operator: " + op}};

            return nlohmann::json{{"result", result}};
        } catch (const std::exception& e) {
            return nlohmann::json{{"error", "Invalid number format"}};
        }
    });
}

template<typename Func>
void ToolRegistry::register_tool(std::string name, Func&& func) {
    tools_[std::move(name)] = std::forward<Func>(func);
}

bool ToolRegistry::has_tool(const std::string& name) const {
    return tools_.count(name) > 0;
}

nlohmann::json ToolRegistry::call_tool(const std::string& name, const std::unordered_map<std::string, std::string>& args) {
    auto it = tools_.find(name);
    if (it == tools_.end()) {
        return nlohmann::json{{"error", "Tool not found: " + name}};
    }

    try {
        return it->second(args);
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", std::string("Tool execution failed: ") + e.what()}};
    }
}

std::vector<std::string> ToolRegistry::list_tools() const {
    std::vector<std::string> names;
    names.reserve(tools_.size());
    for (const auto& [name, _] : tools_) {
        names.push_back(name);
    }
    return names;
}

} // namespace agenticdsl

```
    
## `src/llm/llama_adapter.cpp`
    
```cpp
#include "agenticdsl/llm/llama_adapter.h"
#include <stdexcept>
#include <vector>
#include <cstdlib>

namespace agenticdsl {

LlamaAdapter::LlamaAdapter(const Config& config)
    : config_(config),
      model_(nullptr, llama_model_free),
      ctx_(nullptr, llama_free),
      sampler_(nullptr, llama_sampler_free) {

    // Load model
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = 99; // Use all GPU layers if available

    llama_model* raw_model = llama_model_load_from_file(config_.model_path.c_str(), model_params);
    if (!raw_model) {
        throw std::runtime_error("Failed to load model: " + config_.model_path);
    }
    model_.reset(raw_model);

    // Create context
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = config_.n_ctx;
    ctx_params.n_threads = config_.n_threads;
    ctx_params.n_threads_batch = config_.n_threads;

    llama_context* raw_ctx = llama_init_from_model(model_.get(), ctx_params);
    if (!raw_ctx) {
        throw std::runtime_error("Failed to create context");
    }
    ctx_.reset(raw_ctx);

    // Create sampler chain
    auto smpl_params = llama_sampler_chain_default_params();
    llama_sampler* raw_sampler = llama_sampler_chain_init(smpl_params);
    llama_sampler_chain_add(raw_sampler, llama_sampler_init_min_p(config_.min_p, 1));
    llama_sampler_chain_add(raw_sampler, llama_sampler_init_temp(config_.temperature));
    llama_sampler_chain_add(raw_sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    sampler_.reset(raw_sampler);
}

LlamaAdapter::~LlamaAdapter() = default;

std::vector<llama_token> LlamaAdapter::tokenize(const std::string& text, bool add_bos) {
    const llama_vocab* vocab = llama_model_get_vocab(model_.get());
    int32_t n_tokens = llama_tokenize(vocab, text.data(), static_cast<int32_t>(text.size()),
                                      nullptr, 0, add_bos, true);
    if (n_tokens < 0) return {};

    std::vector<llama_token> tokens(n_tokens);
    if (llama_tokenize(vocab, text.data(), static_cast<int32_t>(text.size()),
                       tokens.data(), n_tokens, add_bos, true) < 0) {
        return {};
    }
    return tokens;
}

std::string LlamaAdapter::detokenize(llama_token token) {
    const llama_vocab* vocab = llama_model_get_vocab(model_.get());
    char buf[256] = {0};
    int n = llama_token_to_piece(vocab, token, buf, sizeof(buf) - 1, 0, true);
    if (n < 0) return "";
    return std::string(buf, n);
}

std::string LlamaAdapter::generate(const std::string& prompt) {
    if (!is_loaded()) {
        throw std::runtime_error("Model not loaded");
    }

    // Check if context is empty (first call)
    bool is_first = llama_memory_seq_pos_max(llama_get_memory(ctx_.get()), 0) == -1;

    auto tokens = tokenize(prompt, is_first);
    if (tokens.empty()) {
        throw std::runtime_error("Tokenization failed");
    }

    // Prepare batch
    llama_batch batch = llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()));

    // Decode prompt
    if (llama_decode(ctx_.get(), batch)) {
        throw std::runtime_error("Prompt evaluation failed");
    }

    std::string response;
    for (int i = 0; i < config_.n_predict; ++i) {
        llama_token new_token = llama_sampler_sample(sampler_.get(), ctx_.get(), -1);

        if (llama_vocab_is_eog(llama_model_get_vocab(model_.get()), new_token)) {
            break;
        }

        std::string piece = detokenize(new_token);
        response += piece;

        // Prepare next token
        batch = llama_batch_get_one(&new_token, 1);
        if (llama_decode(ctx_.get(), batch)) {
            break;
        }
    }

    // Reset sampler state for next call
    llama_sampler_reset(sampler_.get());

    return response;
}

bool LlamaAdapter::is_loaded() const {
    return model_ != nullptr && ctx_ != nullptr && sampler_ != nullptr;
}

} // namespace agenticdsl

```
    
## `src/core/executor.cpp`
    
```cpp
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

```
    
## `src/core/nodes.cpp`
    
```cpp
#include "agenticdsl/core/nodes.h"
#include "agenticdsl/dsl/templates.h"
#include "agenticdsl/tools/registry.h"
#include "agenticdsl/llm/llama_adapter.h"
#include "agenticdsl/resources/manager.h"

#include <inja/inja.hpp>
#include <stdexcept>
#include <string>

namespace agenticdsl {

// StartNode
StartNode::StartNode(NodePath path, std::vector<NodePath> next_paths)
    : Node(std::move(path), NodeType::START, std::move(next_paths)) {}

Context StartNode::execute(Context& context) {
    return context;
}

// EndNode
EndNode::EndNode(NodePath path)
    : Node(std::move(path), NodeType::END) {}

Context EndNode::execute(Context& context) {
    return context;
}

// AssignNode
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

// LLMCallNode
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
        std::string rendered_prompt = InjaTemplateRenderer::render(prompt_template, context);
        // TODO: Replace mock with real LLM call
        std::string llm_response = "[MOCK] Generated response for prompt length: " +
                                   std::to_string(rendered_prompt.length());

        // v1.1: output_keys supports list, but LLM returns string → store in first key
        new_context[output_keys[0]] = llm_response;

    } catch (const inja::RenderError& e) {
        throw std::runtime_error("Prompt template rendering failed: " + std::string(e.what()));
    }
    return new_context;
}

// ToolCallNode
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

// ResourceNode
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

} // namespace agenticdsl

```
    
## `src/core/engine.cpp`
    
```cpp
#include "agenticdsl/core/engine.h"
#include "agenticdsl/llm/llama_adapter.h"
#include <fstream>
#include <sstream>
#include <thread>
#include <stdexcept>

namespace agenticdsl {

std::unique_ptr<AgenticDSLEngine> AgenticDSLEngine::from_markdown(const std::string& markdown_content) {
    MarkdownParser parser;
    auto graphs = parser.parse_from_string(markdown_content);

    // Ensure /main exists
    bool has_main = false;
    for (const auto& g : graphs) {
        if (g.path == "/main") {
            has_main = true;
            break;
        }
    }
    if (!has_main) {
        throw std::runtime_error("Required /main subgraph not found");
    }

    LlamaAdapter::Config config;
    config.model_path = "models/qwen-0.6b.gguf";
    config.n_ctx = 2048;
    config.n_threads = std::thread::hardware_concurrency();
    auto llama_adapter = std::make_unique<LlamaAdapter>(config);

    auto engine = std::make_unique<AgenticDSLEngine>(std::move(graphs));
    engine->llama_adapter_ = std::move(llama_adapter);
    return engine;
}

std::unique_ptr<AgenticDSLEngine> AgenticDSLEngine::from_file(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + file_path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return from_markdown(buffer.str());
}

AgenticDSLEngine::AgenticDSLEngine(std::vector<ParsedGraph> initial_graphs)
    : full_graphs_(std::move(initial_graphs)) {
    executor_ = std::make_unique<DAGFlowExecutor>(full_graphs_);
}

AgenticDSLEngine::ExecutionResult AgenticDSLEngine::run(const Context& context) {
    auto result = executor_->execute(context);
    return {result.success, result.message, result.final_context, result.paused_at};
}

void AgenticDSLEngine::append_graphs(const std::vector<ParsedGraph>& new_graphs) {
    full_graphs_.insert(full_graphs_.end(), new_graphs.begin(), new_graphs.end());
    executor_ = std::make_unique<DAGFlowExecutor>(full_graphs_);
}
} // namespace agenticdsl

```
    
## `src/core/parser.cpp`
    
```cpp
#include "agenticdsl/core/parser.h"
#include "agenticdsl/core/nodes.h"
#include "common/utils.h" // v1.1: corrected include
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace agenticdsl {

std::vector<ParsedGraph> MarkdownParser::parse_from_string(const std::string& markdown_content) {
    std::vector<ParsedGraph> graphs;
    auto pathed_blocks = extract_pathed_blocks(markdown_content);

    for (auto& [path, yaml_content] : pathed_blocks) {
        if (!is_valid_node_path(path)) {
            throw std::runtime_error("Invalid node path format: " + path);
        }

        try {
            auto json_doc = nlohmann::json::parse(yaml_content);

            // Handle file-level metadata (e.g., /__meta__)
            if (path == "/__meta__") {
                // Optional: store in global metadata
                continue;
            }

            // Handle subgraph (e.g., /main)
            if (json_doc.contains("graph_type") && json_doc["graph_type"] == "subgraph") {
                ParsedGraph graph;
                graph.path = path;
                graph.metadata = json_doc.value("metadata", nlohmann::json::object());

                if (json_doc.contains("nodes") && json_doc["nodes"].is_array()) {
                    for (const auto& node_json : json_doc["nodes"]) {
                        std::string id = node_json.value("id", "");
                        if (id.empty()) {
                            throw std::runtime_error("Node in subgraph '" + path + "' missing 'id'");
                        }
                        NodePath node_path = path + "/" + id;
                        auto node = create_node_from_json(node_path, node_json);
                        if (node) {
                            graph.nodes.push_back(std::move(node));
                        }
                    }
                }
                graphs.push_back(std::move(graph));
                continue;
            }

            // Handle single node
            if (json_doc.contains("type")) {
                auto node = create_node_from_json(path, json_doc);
                if (node) {
                    ParsedGraph graph;
                    graph.path = path;
                    graph.metadata = json_doc.value("metadata", nlohmann::json::object());
                    graph.nodes.push_back(std::move(node));
                    graphs.push_back(std::move(graph));
                }
            }

        } catch (const nlohmann::json::parse_error& e) {
            throw std::runtime_error("JSON parse error in block '" + path + "': " + std::string(e.what()));
        } catch (const std::exception& e) {
            throw std::runtime_error("Error parsing block '" + path + "': " + std::string(e.what()));
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
    return parse_from_string(buffer.str());
}

std::unique_ptr<Node> MarkdownParser::create_node_from_json(const NodePath& path, const nlohmann::json& node_json) {
    std::string type_str = node_json.at("type").get<std::string>();

    // Parse next
    std::vector<NodePath> next_paths;
    if (node_json.contains("next")) {
        const auto& next = node_json["next"];
        if (next.is_string()) {
            next_paths.push_back(next.get<std::string>());
        } else if (next.is_array()) {
            for (const auto& np : next) {
                next_paths.push_back(np.get<std::string>());
            }
        }
    }

    // Parse metadata
    nlohmann::json metadata = node_json.value("metadata", nlohmann::json::object());

    if (type_str == "start") {
        return std::make_unique<StartNode>(path, std::move(next_paths));
    } else if (type_str == "end") {
        auto node = std::make_unique<EndNode>(path);
        node->metadata = metadata;
        return node;
    } else if (type_str == "assign") {
        std::unordered_map<std::string, std::string> assign;
        if (node_json.contains("assign") && node_json["assign"].is_object()) {
            for (auto& [key, value] : node_json["assign"].items()) {
                assign[key] = value.get<std::string>();
            }
        }
        return std::make_unique<AssignNode>(path, std::move(assign), std::move(next_paths));
    } else if (type_str == "llm_call") {
        std::string prompt = node_json.at("prompt_template").get<std::string>();
        auto output_keys = parse_output_keys(node_json, path);
        return std::make_unique<LLMCallNode>(path, std::move(prompt), std::move(output_keys), std::move(next_paths));
    } else if (type_str == "tool_call") {
        std::string tool = node_json.at("tool").get<std::string>();
        auto output_keys = parse_output_keys(node_json, path);
        std::unordered_map<std::string, std::string> args;
        if (node_json.contains("arguments") && node_json["arguments"].is_object()) {
            for (auto& [key, value] : node_json["arguments"].items()) {
                args[key] = value.get<std::string>();
            }
        }
        return std::make_unique<ToolCallNode>(path, std::move(tool), std::move(args), std::move(output_keys), std::move(next_paths));
    } else if (type_str == "resource") {
        std::string type_str_lower = node_json.at("resource_type").get<std::string>();
        ResourceType rtype = parse_resource_type(type_str_lower);
        std::string uri = node_json.at("uri").get<std::string>();
        std::string scope = node_json.value("scope", std::string("global"));
        return std::make_unique<ResourceNode>(path, rtype, std::move(uri), std::move(scope), metadata);
    }

    // Unknown type: ignore (forward-compatible per spec)
    return nullptr;
}

void MarkdownParser::validate_nodes(const std::vector<std::unique_ptr<Node>>& nodes) {
    // Optional: implement validation (e.g., path uniqueness, next existence)
    // For v1, rely on executor-level validation
}

} // namespace agenticdsl

```
    
## `src/resources/manager.cpp`
    
```cpp
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
    
## `src/dsl/spec.cpp`
    
```cpp
#include "agenticdsl/dsl/spec.h"
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace agenticdsl {

void NodeValidator::validate(const nlohmann::json& node_json) {
    if (!node_json.is_object()) {
        throw std::runtime_error("Node must be a JSON object");
    }

    if (!node_json.contains("type") || !node_json["type"].is_string()) {
        throw std::runtime_error("Missing or invalid 'type' field");
    }

    std::string type = node_json["type"];
    validate_type(type);

    if (type == "start") validate_start(node_json);
    else if (type == "end") validate_end(node_json);
    else if (type == "assign") validate_assign(node_json);
    else if (type == "llm_call") validate_llm_call(node_json);
    else if (type == "tool_call") validate_tool_call(node_json);
    else if (type == "resource") validate_resource(node_json);
    // Unknown types are allowed (forward compatibility)
}

void NodeValidator::validate_type(const std::string& type) {
    static const std::unordered_set<std::string> valid_types = {
        "start", "end", "assign", "llm_call", "tool_call", "resource"
        // codelet etc. allowed but not validated in v1
    };
    if (valid_types.count(type) == 0) {
        // Allow unknown types for forward compatibility (per spec §4)
        // Do nothing, or log warning if needed
    }
}

void NodeValidator::validate_start(const nlohmann::json& node) {
    if (!node.contains("next")) {
        throw std::runtime_error("start node missing 'next'");
    }
    validate_next(node);
}

void NodeValidator::validate_end(const nlohmann::json& node) {
    // end node has no required fields beyond 'type'
    if (node.contains("output_keys")) {
        validate_output_keys(node);
    }
}

void NodeValidator::validate_assign(const nlohmann::json& node) {
    if (!node.contains("assign") || !node["assign"].is_object()) {
        throw std::runtime_error("assign node requires 'assign' object");
    }
    if (!node.contains("next")) {
        throw std::runtime_error("assign node missing 'next'");
    }
    validate_next(node);
}

void NodeValidator::validate_llm_call(const nlohmann::json& node) {
    if (!node.contains("prompt_template") || !node["prompt_template"].is_string()) {
        throw std::runtime_error("llm_call node requires 'prompt_template' string");
    }
    if (!node.contains("output_keys")) {
        throw std::runtime_error("llm_call node requires 'output_keys'");
    }
    validate_output_keys(node);
    if (!node.contains("next")) {
        throw std::runtime_error("llm_call node missing 'next'");
    }
    validate_next(node);
}

void NodeValidator::validate_tool_call(const nlohmann::json& node) {
    if (!node.contains("tool") || !node["tool"].is_string()) {
        throw std::runtime_error("tool_call node requires 'tool' string");
    }
    if (!node.contains("arguments") || !node["arguments"].is_object()) {
        throw std::runtime_error("tool_call node requires 'arguments' object");
    }
    if (!node.contains("output_keys")) {
        throw std::runtime_error("tool_call node requires 'output_keys'");
    }
    validate_output_keys(node);
    if (!node.contains("next")) {
        throw std::runtime_error("tool_call node missing 'next'");
    }
    validate_next(node);
}

void NodeValidator::validate_resource(const nlohmann::json& node) {
    if (!node.contains("resource_type") || !node["resource_type"].is_string()) {
        throw std::runtime_error("resource node requires 'resource_type' string");
    }
    if (!node.contains("uri") || !node["uri"].is_string()) {
        throw std::runtime_error("resource node requires 'uri' string");
    }
    // 'scope' is optional
}

void NodeValidator::validate_output_keys(const nlohmann::json& node) {
    const auto& ok = node["output_keys"];
    if (!ok.is_string() && !ok.is_array()) {
        throw std::runtime_error("'output_keys' must be string or array");
    }
    if (ok.is_array()) {
        for (const auto& item : ok) {
            if (!item.is_string()) {
                throw std::runtime_error("All items in 'output_keys' array must be strings");
            }
        }
    }
}

void NodeValidator::validate_next(const nlohmann::json& node) {
    const auto& next = node["next"];
    if (!next.is_string() && !next.is_array()) {
        throw std::runtime_error("'next' must be string or array of strings");
    }
    if (next.is_array()) {
        for (const auto& item : next) {
            if (!item.is_string()) {
                throw std::runtime_error("All items in 'next' array must be strings");
            }
        }
    }
}

} // namespace agenticdsl

```
    
## `src/dsl/templates.cpp`
    
```cpp
#include "agenticdsl/dsl/templates.h"
#include <inja/inja.hpp>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace agenticdsl {

InjaTemplateRenderer::InjaTemplateRenderer() : env_() {
    env_.set_expression("{{", "}}");
    env_.set_statement("{%", "%}");
    env_.set_comment("{#", "#}");
    env_.set_line_statement("##");

    configure_security();

    // Inja v3: add_callback(name, func) — NO num_args!
    env_.add_callback("default", [](inja::Arguments& args) -> nlohmann::json {
        return args[0]->is_null() ? *args[1] : *args[0];
    });

    env_.add_callback("exists", [](inja::Arguments& args) -> nlohmann::json {
        return !args[0]->is_null();
    });

    env_.add_callback("length", [](inja::Arguments& args) -> nlohmann::json {
        if (args[0]->is_string()) {
            return static_cast<int>(args[0]->get<std::string>().size());
        } else if (args[0]->is_array()) {
            return static_cast<int>(args[0]->size());
        }
        return 0;
    });

    env_.add_callback("join", [](inja::Arguments& args) -> nlohmann::json {
        std::string sep = args[1]->get<std::string>();
        std::string result;
        const auto& arr = *args[0];
        for (size_t i = 0; i < arr.size(); ++i) {
            if (i > 0) result += sep;
            result += arr[i].get<std::string>();
        }
        return result;
    });

    env_.add_callback("upper", [](inja::Arguments& args) -> nlohmann::json {
        std::string s = args[0]->get<std::string>();
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        return s;
    });

    env_.add_callback("lower", [](inja::Arguments& args) -> nlohmann::json {
        std::string s = args[0]->get<std::string>();
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    });
}

void InjaTemplateRenderer::configure_security() {
    // Inja v3: set_include_callback expects a function returning inja::Template
    env_.set_include_callback([](const std::filesystem::path&, const std::string&) -> inja::Template {
        throw inja::InjaError("render_error", "Include is disabled for security.", inja::SourceLocation{});
    });
}

std::string InjaTemplateRenderer::render(std::string_view template_str, const Context& context) {
    static InjaTemplateRenderer renderer;
    try {
        return renderer.env_.render(template_str, context);
    } catch (const inja::InjaError& e) {
        throw std::runtime_error("Template render error: " + e.message);
    }
}

std::string InjaTemplateRenderer::render_with_env(std::string_view template_str, const Context& context) {
    try {
        return env_.render(template_str, context);
    } catch (const inja::InjaError& e) {
        throw std::runtime_error("Template render error: " + e.message);
    }
}

} // namespace agenticdsl

```
    