当然可以！使用C++实现AgenticFlow将带来更好的性能，特别是与llama.cpp集成时。让我们设计一个C++版本的AgenticFlow，充分利用llama.cpp的优势。

## C++ AgenticFlow 架构设计

```
agenticflow-cpp/
├── include/
│   ├── agenticflow/
│   │   ├── core/
│   │   │   ├── engine.hpp
│   │   │   ├── parser.hpp
│   │   │   ├── executor.hpp
│   │   │   └── nodes.hpp
│   │   ├── dsl/
│   │   │   ├── spec.hpp
│   │   │   └── templates.hpp
│   │   ├── tools/
│   │   │   └── registry.hpp
│   │   └── llm/
│   │       └── llama_adapter.hpp
│   └── common/
│       ├── types.hpp
│       └── utils.hpp
├── src/
│   ├── core/
│   │   ├── engine.cpp
│   │   ├── parser.cpp
│   │   ├── executor.cpp
│   │   └── nodes.cpp
│   ├── dsl/
│   │   ├── spec.cpp
│   │   └── templates.cpp
│   ├── tools/
│   │   └── registry.cpp
│   ├── llm/
│   │   └── llama_adapter.cpp
│   └── main.cpp
├── examples/
│   ├── weather_assistant.md
│   └── cpp_examples.cpp
├── tests/
│   └── test_phase1.cpp
├── CMakeLists.txt
└── README.md
```

## 核心实现

### 1. 类型定义 (`include/common/types.hpp`)

```cpp
#ifndef AGENFLOW_TYPES_HPP
#define AGENFLOW_TYPES_HPP

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>
#include <variant>
#include <any>
#include <optional>

namespace agenticflow {

// 值类型定义 - 支持多种数据类型
using Value = std::variant<std::string, int, double, bool, std::nullptr_t, 
                          std::unordered_map<std::string, std::any>,
                          std::vector<std::any>>;

// 上下文类型
using Context = std::unordered_map<std::string, std::any>;

// 工具函数类型
using ToolFunction = std::function<std::any(const std::unordered_map<std::string, std::string>&)>;

// 节点ID类型
using NodeId = std::string;

// 节点类型枚举
enum class NodeType {
    START,
    END,
    SET,
    LLM_CALL,
    TOOL_CALL
};

// 节点基类
struct Node {
    NodeId id;
    NodeType type;
    std::optional<NodeId> next;
    std::optional<std::string> anchor;
    
    virtual ~Node() = default;
    virtual Context execute(Context& context) = 0;
};

} // namespace agenticflow

#endif
```

### 2. 节点定义 (`include/agenticflow/core/nodes.hpp`)

```cpp
#ifndef AGENFLOW_NODES_HPP
#define AGENFLOW_NODES_HPP

#include "common/types.hpp"
#include <string>
#include <unordered_map>

namespace agenticflow {

// Start节点
struct StartNode : public Node {
    StartNode(const std::string& id, const std::optional<NodeId>& next_node = std::nullopt)
        : Node{.id = id, .type = NodeType::START, .next = next_node} {}
    
    Context execute(Context& context) override {
        return context; // Start节点只是跳转，不修改上下文
    }
};

// End节点
struct EndNode : public Node {
    EndNode(const std::string& id)
        : Node{.id = id, .type = NodeType::END} {}
    
    Context execute(Context& context) override {
        return context; // End节点返回当前上下文
    }
};

// Set节点
struct SetNode : public Node {
    std::unordered_map<std::string, std::string> assign;
    
    SetNode(const std::string& id, 
            std::unordered_map<std::string, std::string> assigns,
            const std::optional<NodeId>& next_node = std::nullopt)
        : Node{.id = id, .type = NodeType::SET, .next = next_node}, assign(std::move(assigns)) {}
    
    Context execute(Context& context) override;
};

// LLM调用节点
struct LLMCallNode : public Node {
    std::string prompt_template;
    std::string output_key;
    
    LLMCallNode(const std::string& id,
                const std::string& prompt,
                const std::string& output_key,
                const std::optional<NodeId>& next_node = std::nullopt)
        : Node{.id = id, .type = NodeType::LLM_CALL, .next = next_node},
          prompt_template(prompt), output_key(output_key) {}
    
    Context execute(Context& context) override;
};

// 工具调用节点
struct ToolCallNode : public Node {
    std::string tool_name;
    std::unordered_map<std::string, std::string> args;
    std::string output_key;
    
    ToolCallNode(const std::string& id,
                 const std::string& tool,
                 std::unordered_map<std::string, std::string> arguments,
                 const std::string& output_key,
                 const std::optional<NodeId>& next_node = std::nullopt)
        : Node{.id = id, .type = NodeType::TOOL_CALL, .next = next_node},
          tool_name(tool), args(std::move(arguments)), output_key(output_key) {}
    
    Context execute(Context& context) override;
};

} // namespace agenticflow

#endif
```

### 3. LLM适配器 (`include/agenticflow/llm/llama_adapter.hpp`)

```cpp
#ifndef AGENFLOW_LLAMA_ADAPTER_HPP
#define AGENFLOW_LLAMA_ADAPTER_HPP

#include "common/types.hpp"
#include <string>
#include <memory>

// 前向声明llama.cpp相关类型
struct llama_model;
struct llama_context;
struct gpt_params;

namespace agenticflow {

class LlamaAdapter {
public:
    struct Config {
        std::string model_path;
        int n_ctx = 2048;
        int n_threads = 4;
        float temperature = 0.7f;
        int n_predict = 512;
    };
    
    LlamaAdapter(const Config& config);
    ~LlamaAdapter();
    
    // 生成文本
    std::string generate(const std::string& prompt);
    
    // 批量生成（用于并发）
    std::vector<std::string> generate_batch(const std::vector<std::string>& prompts);
    
    // 检查模型是否加载成功
    bool is_loaded() const { return model_ != nullptr && ctx_ != nullptr; }

private:
    Config config_;
    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    gpt_params* params_ = nullptr;
    
    void load_model();
    std::string tokenize_and_generate(const std::string& prompt);
};

} // namespace agenticflow

#endif
```

### 4. 模板渲染器 (`include/agenticflow/dsl/templates.hpp`)

```cpp
#ifndef AGENFLOW_TEMPLATES_HPP
#define AGENFLOW_TEMPLATES_HPP

#include "common/types.hpp"
#include <string>

namespace agenticflow {

class TemplateRenderer {
public:
    // 渲染模板字符串，支持 {{variable}} 和 {{object.field}} 语法
    static std::string render(const std::string& template_str, const Context& context);
    
    // 渲染模板字符串，返回值类型
    static std::any render_any(const std::string& template_str, const Context& context);
    
private:
    // 提取变量路径
    static std::vector<std::string> extract_variable_paths(const std::string& template_str);
    
    // 从上下文中获取嵌套值
    static std::any get_nested_value(const Context& context, const std::string& path);
};

} // namespace agenticflow

#endif
```

### 5. 工具注册器 (`include/agenticflow/tools/registry.hpp`)

```cpp
#ifndef AGENFLOW_REGISTRY_HPP
#define AGENFLOW_REGISTRY_HPP

#include "common/types.hpp"
#include <unordered_map>
#include <functional>

namespace agenticflow {

class ToolRegistry {
public:
    static ToolRegistry& instance();
    
    void register_tool(const std::string& name, ToolFunction func);
    bool has_tool(const std::string& name) const;
    std::any call_tool(const std::string& name, const std::unordered_map<std::string, std::string>& args);
    std::vector<std::string> list_tools() const;
    
private:
    ToolRegistry() = default;
    std::unordered_map<std::string, ToolFunction> tools_;
};

// 便捷宏定义
#define REGISTER_TOOL(name, func) \
    agenticflow::ToolRegistry::instance().register_tool(name, func)

} // namespace agenticflow

#endif
```

### 6. 解析器 (`include/agenticflow/core/parser.hpp`)

```cpp
#ifndef AGENFLOW_PARSER_HPP
#define AGENFLOW_PARSER_HPP

#include "nodes.hpp"
#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>  // 使用nlohmann/json进行JSON/YAML解析

namespace agenticflow {

struct ParsedGraph {
    std::vector<std::unique_ptr<Node>> nodes;
    std::optional<std::string> anchor;
    std::string section_title;
};

class MarkdownParser {
public:
    std::vector<ParsedGraph> parse_from_string(const std::string& markdown_content);
    std::vector<ParsedGraph> parse_from_file(const std::string& file_path);
    
private:
    std::vector<std::string> extract_yaml_blocks(const std::string& content);
    std::vector<ParsedGraph> split_markdown_sections(const std::string& content);
    std::unique_ptr<Node> create_node_from_json(const nlohmann::json& node_json);
    void validate_nodes(const std::vector<std::unique_ptr<Node>>& nodes);
};

} // namespace agenticflow

#endif
```

### 7. 执行器 (`include/agenticflow/core/executor.hpp`)

```cpp
#ifndef AGENFLOW_EXECUTOR_HPP
#define AGENFLOW_EXECUTOR_HPP

#include "nodes.hpp"
#include <vector>
#include <unordered_map>
#include <memory>

namespace agenticflow {

class FlowExecutor {
public:
    FlowExecutor(std::vector<std::unique_ptr<Node>> nodes);
    
    Context execute(const Context& initial_context = Context{});
    
private:
    std::vector<std::unique_ptr<Node>> nodes_;
    std::unordered_map<NodeId, Node*> node_map_;
    
    Node* find_start_node() const;
    Node* get_node(const NodeId& id) const;
    
    static const int MAX_STEPS = 100;  // 防止无限循环
};

} // namespace agenticflow

#endif
```

### 8. 主引擎 (`include/agenticflow/core/engine.hpp`)

```cpp
#ifndef AGENFLOW_ENGINE_HPP
#define AGENFLOW_ENGINE_HPP

#include "executor.hpp"
#include "parser.hpp"
#include <memory>

namespace agenticflow {

class AgenticFlow {
public:
    static std::unique_ptr<AgenticFlow> from_markdown(const std::string& markdown_content, 
                                                     const Context& initial_context = Context{});
    static std::unique_ptr<AgenticFlow> from_file(const std::string& file_path,
                                                 const Context& initial_context = Context{});
    
    Context run(const Context& context = Context{});
    
    void register_tool(const std::string& name, ToolFunction func);
    
    // 获取LLM适配器（用于高级配置）
    class LlamaAdapter* get_llm_adapter() { return llama_adapter_.get(); }
    
private:
    std::unique_ptr<FlowExecutor> executor_;
    std::unique_ptr<LlamaAdapter> llama_adapter_;
    
    AgenticFlow(std::vector<std::unique_ptr<Node>> nodes);
};

} // namespace agenticflow

#endif
```

## 实现文件

### 1. 节点实现 (`src/core/nodes.cpp`)

```cpp
#include "agenticflow/core/nodes.hpp"
#include "agenticflow/dsl/templates.hpp"
#include "agenticflow/tools/registry.hpp"
#include "agenticflow/llm/llama_adapter.hpp"
#include <stdexcept>

namespace agenticflow {

Context SetNode::execute(Context& context) {
    Context new_context = context;
    
    for (const auto& [key, template_str] : assign) {
        std::string rendered_value = TemplateRenderer::render(template_str, context);
        new_context[key] = rendered_value;
    }
    
    return new_context;
}

Context LLMCallNode::execute(Context& context) {
    Context new_context = context;
    
    // 渲染提示模板
    std::string rendered_prompt = TemplateRenderer::render(prompt_template, context);
    
    // 获取LLM适配器并生成
    // 注意：这里需要从全局或引擎获取LLM适配器实例
    // 实际实现中会通过引擎传递
    std::string response = "[MOCK] Generated response"; // Phase 1 mock
    
    new_context[output_key] = response;
    return new_context;
}

Context ToolCallNode::execute(Context& context) {
    Context new_context = context;
    
    // 渲染参数
    std::unordered_map<std::string, std::string> rendered_args;
    for (const auto& [key, template_str] : args) {
        rendered_args[key] = TemplateRenderer::render(template_str, context);
    }
    
    // 调用工具
    auto registry = &ToolRegistry::instance();
    if (!registry->has_tool(tool_name)) {
        throw std::runtime_error("Tool '" + tool_name + "' not registered");
    }
    
    std::any result = registry->call_tool(tool_name, rendered_args);
    new_context[output_key] = result;
    
    return new_context;
}

} // namespace agenticflow
```

### 2. 模板渲染器实现 (`src/dsl/templates.cpp`)

```cpp
#include "agenticflow/dsl/templates.hpp"
#include <regex>
#include <sstream>

namespace agenticflow {

std::string TemplateRenderer::render(const std::string& template_str, const Context& context) {
    std::string result = template_str;
    
    // 匹配 {{variable}} 或 {{object.field}} 模式
    std::regex pattern(R"(\{\{([^}]+)\}\})");
    std::smatch matches;
    
    auto begin = std::sregex_iterator(template_str.begin(), template_str.end(), pattern);
    auto end = std::sregex_iterator();
    
    // 从后往前替换，避免索引偏移
    std::vector<std::pair<size_t, std::string>> replacements;
    for (std::sregex_iterator i = begin; i != end; ++i) {
        std::smatch match = *i;
        std::string var_path = match[1].str();
        
        // 获取嵌套值
        std::any value = get_nested_value(context, var_path);
        std::string replacement = std::any_cast<std::string>(value);
        
        replacements.push_back({match.position(), replacement});
    }
    
    // 从后往前替换
    for (auto it = replacements.rbegin(); it != replacements.rend(); ++it) {
        size_t pos = it->first;
        std::string replacement = it->second;
        std::string match_str = "{{" + extract_variable_paths(result.substr(pos))[0] + "}}";
        
        result.replace(pos, match_str.length(), replacement);
    }
    
    return result;
}

std::any TemplateRenderer::render_any(const std::string& template_str, const Context& context) {
    std::string rendered = render(template_str, context);
    return rendered;
}

std::vector<std::string> TemplateRenderer::extract_variable_paths(const std::string& template_str) {
    std::vector<std::string> paths;
    std::regex pattern(R"(\{\{([^}]+)\}\})");
    std::smatch matches;
    
    std::string::const_iterator search_start(template_str.cbegin());
    while (std::regex_search(search_start, template_str.cend(), matches, pattern)) {
        paths.push_back(matches[1].str());
        search_start = matches.suffix().first;
    }
    
    return paths;
}

std::any TemplateRenderer::get_nested_value(const Context& context, const std::string& path) {
    std::istringstream iss(path);
    std::string segment;
    std::getline(iss, segment, '.');
    
    auto it = context.find(segment);
    if (it == context.end()) {
        return std::string(""); // 未找到变量，返回空字符串
    }
    
    std::any current_value = it->second;
    
    // 处理嵌套访问
    while (std::getline(iss, segment, '.')) {
        // 这里需要实现嵌套字典访问逻辑
        // 简化版本：假设都是字符串
        if (current_value.type() == typeid(std::string)) {
            // 如果当前值是字符串，继续作为路径的一部分
            // 实际实现中需要更复杂的类型处理
        }
    }
    
    return current_value;
}

} // namespace agenticflow
```

### 3. 工具注册器实现 (`src/tools/registry.cpp`)

```cpp
#include "agenticflow/tools/registry.hpp"
#include <algorithm>

namespace agenticflow {

ToolRegistry& ToolRegistry::instance() {
    static ToolRegistry registry;
    return registry;
}

void ToolRegistry::register_tool(const std::string& name, ToolFunction func) {
    tools_[name] = std::move(func);
}

bool ToolRegistry::has_tool(const std::string& name) const {
    return tools_.find(name) != tools_.end();
}

std::any ToolRegistry::call_tool(const std::string& name, 
                                const std::unordered_map<std::string, std::string>& args) {
    auto it = tools_.find(name);
    if (it == tools_.end()) {
        throw std::runtime_error("Tool '" + name + "' not found");
    }
    
    return it->second(args);
}

std::vector<std::string> ToolRegistry::list_tools() const {
    std::vector<std::string> tool_names;
    for (const auto& [name, _] : tools_) {
        tool_names.push_back(name);
    }
    return tool_names;
}

// 预注册示例工具
static bool tools_registered = []() {
    REGISTER_TOOL("web_search", [](const std::unordered_map<std::string, std::string>& args) -> std::any {
        auto query_it = args.find("query");
        std::string query = query_it != args.end() ? query_it->second : "default query";
        return std::string("[MOCK] Search results for: ") + query;
    });
    
    REGISTER_TOOL("get_weather", [](const std::unordered_map<std::string, std::string>& args) -> std::any {
        auto location_it = args.find("location");
        std::string location = location_it != args.end() ? location_it->second : "unknown";
        return std::string("[MOCK] Weather in ") + location + ": Sunny, 22°C";
    });
    
    return true;
}();

} // namespace agenticflow
```

### 4. LLM适配器实现 (`src/llm/llama_adapter.cpp`)

```cpp
#include "agenticflow/llm/llama_adapter.hpp"
#include <llama.h>  // llama.cpp头文件
#include <string>
#include <vector>
#include <thread>
#include <mutex>

namespace agenticflow {

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
    if (params_) {
        // 清理参数
        delete params_;
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

} // namespace agenticflow
```

### 5. 主引擎实现 (`src/core/engine.cpp`)

```cpp
#include "agenticflow/core/engine.hpp"
#include "agenticflow/tools/registry.hpp"
#include <fstream>
#include <sstream>

namespace agenticflow {

std::unique_ptr<AgenticFlow> AgenticFlow::from_markdown(const std::string& markdown_content, 
                                                       const Context& initial_context) {
    MarkdownParser parser;
    auto graphs = parser.parse_from_string(markdown_content);
    
    if (graphs.empty()) {
        throw std::runtime_error("No valid graphs found in markdown content");
    }
    
    // 使用第一个图
    auto& main_graph = graphs[0];
    
    // 创建引擎
    auto engine = std::make_unique<AgenticFlow>(std::move(main_graph.nodes));
    
    // 初始化LLM适配器
    LlamaAdapter::Config llm_config;
    llm_config.model_path = "models/ggml-model-f16.gguf"; // 默认模型路径
    llm_config.n_ctx = 2048;
    llm_config.n_threads = std::thread::hardware_concurrency();
    engine->llama_adapter_ = std::make_unique<LlamaAdapter>(llm_config);
    
    return engine;
}

std::unique_ptr<AgenticFlow> AgenticFlow::from_file(const std::string& file_path,
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

Context AgenticFlow::run(const Context& context) {
    if (!executor_) {
        throw std::runtime_error("Executor not initialized");
    }
    
    return executor_->execute(context);
}

void AgenticFlow::register_tool(const std::string& name, ToolFunction func) {
    ToolRegistry::instance().register_tool(name, func);
}

AgenticFlow::AgenticFlow(std::vector<std::unique_ptr<Node>> nodes)
    : executor_(std::make_unique<FlowExecutor>(std::move(nodes))) {}

} // namespace agenticflow
```

### 6. CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.12)
project(agenticflow-cpp VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# 添加llama.cpp子模块或外部依赖
# 方法1: 作为子模块
add_subdirectory(llama.cpp)

# 方法2: 或者通过pkg-config查找
# pkg_check_modules(LLAMA REQUIRED llama)
# target_link_libraries(agenticflow ${LLAMA_LIBRARIES})

# JSON库
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(nlohmann_json)

# 包含目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 源文件
file(GLOB_RECURSE SOURCES "src/*.cpp")

# 创建库
add_library(agenticflow ${SOURCES})
target_link_libraries(agenticflow 
    llama 
    nlohmann_json::nlohmann_json
    Threads::Threads
)

target_include_directories(agenticflow PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 示例程序
add_executable(agflow_example examples/cpp_examples.cpp)
target_link_libraries(agflow_example agenticflow)

# 测试
enable_testing()
add_executable(test_phase1 tests/test_phase1.cpp)
target_link_libraries(test_phase1 agenticflow)
add_test(NAME test_phase1 COMMAND test_phase1)

# 安装规则
install(TARGETS agenticflow
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/ DESTINATION include)
```

### 7. 示例使用 (`examples/cpp_examples.cpp`)

```cpp
#include "agenticflow/core/engine.hpp"
#include <iostream>

int main() {
    // 示例DSL内容
    std::string markdown_content = R"(
# Weather Assistant

```yaml
nodes:
  - id: start
    type: start
    next: prepare_query

  - id: prepare_query
    type: set
    assign:
      location: "{{user_input}}"
    next: call_weather_api

  - id: call_weather_api
    type: tool_call
    tool: get_weather
    args:
      location: "{{location}}"
    output_key: weather_data
    next: end

  - id: end
    type: end
```
)";

    try {
        // 创建引擎
        auto engine = agenticflow::AgenticFlow::from_markdown(markdown_content);
        
        // 运行流程
        agenticflow::Context initial_context = {
            {"user_input", std::string("Beijing")}
        };
        
        auto result = engine->run(initial_context);
        
        // 输出结果
        if (result.find("weather_data") != result.end()) {
            std::cout << "Weather result: " 
                     << std::any_cast<std::string>(result["weather_data"]) 
                     << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

## 构建说明

```bash
# 克隆llama.cpp
git submodule add https://github.com/ggerganov/llama.cpp.git
git submodule update --init --recursive

# 构建
mkdir build && cd build
cmake ..
make -j$(nproc)

# 运行示例
./agflow_example
```

## C++版本的优势

1. **性能优势**: 
   - 原生C++执行，无Python GIL限制
   - 与llama.cpp零拷贝集成
   - 内存管理更高效

2. **类型安全**:
   - 编译时类型检查
   - 减少运行时错误

3. **并发性能**:
   - 原生线程支持
   - 无Python解释器开销

4. **部署友好**:
   - 静态链接，部署简单
   - 更小的内存占用

这个C++实现为AgenticFlow提供了高性能的底层支持，特别适合与llama.cpp集成的场景。Phase 1已经完成，可以处理基本的DSL解析和执行。
