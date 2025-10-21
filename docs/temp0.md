好的，我们基于 `inja` 库来重构 AgenticFlow 的 C++20 实现。`inja` 提供了更强大的模板功能，更适合处理可能变得复杂的 LLM 提示模板和 DSL 结构。

## 重构后的现代C++20 AgenticFlow (使用inja)

```
agenticflow-cpp20-inja/
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
│   └── main.cpp
├── examples/
│   └── modern_cpp_examples.cpp
├── tests/
│   └── test_modern.cpp
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
#include <concepts>
#include <string_view>
#include <nlohmann/json.hpp> // inja 依赖 nlohmann/json

namespace agenticflow {

// 使用 nlohmann::json 作为统一的数据类型
using Value = nlohmann::json;
using Context = nlohmann::json;

// 编译期字符串常量
template<size_t N>
struct FixedString {
    char data[N] = {};
    constexpr FixedString(const char (&str)[N]) {
        std::copy_n(str, N, data);
    }
    constexpr operator std::string_view() const { return {data, N-1}; }
};

// 节点ID类型
using NodeId = std::string;

// 节点类型枚举
enum class NodeType : uint8_t {
    START,
    END,
    SET,
    LLM_CALL,
    TOOL_CALL
};

// 工具函数类型 - 现在使用 nlohmann::json
using ToolFunction = std::function<nlohmann::json(const std::unordered_map<std::string, std::string>&)>;

} // namespace agenticflow

#endif
```

### 2. Inja 模板渲染器 (`include/agenticflow/dsl/templates.hpp`)

```cpp
#ifndef AGENFLOW_TEMPLATES_HPP
#define AGENFLOW_TEMPLATES_HPP

#include "common/types.hpp"
#include <inja/inja.hpp>
#include <string>
#include <string_view>

namespace agenticflow {

class InjaTemplateRenderer {
public:
    InjaTemplateRenderer() : env_() {
        // 配置 inja 环境
        env_.set_expression("{{", "}}"); // 默认
        env_.set_statement("{%", "%}"); // 默认
        env_.set_comment("{#", "#}"); // 默认
        env_.set_line_statement("##"); // 默认

        // 可以根据需要启用其他选项
        // env_.set_trim_blocks(true);
        // env_.set_lstrip_blocks(true);
        // env_.set_html_autoescape(false); // 默认不转义
    }

    // 使用 inja 渲染模板
    static std::string render(std::string_view template_str, const Context& context) {
        // 静态环境实例用于简单渲染
        static inja::Environment env;
        try {
            inja::Template temp = env.parse(template_str);
            return env.render(temp, context);
        } catch (const std::exception& e) {
            // 如果渲染失败，返回原始模板或抛出异常
            // 这里选择返回原始模板以避免中断执行流程
            return std::string(template_str);
        }
    }

    // 使用实例环境进行更复杂的渲染（如包含回调）
    std::string render_with_env(std::string_view template_str, const Context& context) {
        try {
            inja::Template temp = env_.parse(template_str);
            return env_.render(temp, context);
        } catch (const std::exception& e) {
            return std::string(template_str);
        }
    }

    // 添加自定义回调函数
    template<typename Func>
    void add_callback(const std::string& name, size_t num_args, Func&& func) {
        env_.add_callback(name, num_args, std::forward<Func>(func));
    }

    // 添加无返回值的回调（例如日志）
    template<typename Func>
    void add_void_callback(const std::string& name, size_t num_args, Func&& func) {
        env_.add_void_callback(name, num_args, std::forward<Func>(func));
    }

private:
    inja::Environment env_;
};

} // namespace agenticflow

#endif
```

### 3. 现代节点定义 (`include/agenticflow/core/nodes.hpp`)

```cpp
#ifndef AGENFLOW_NODES_HPP
#define AGENFLOW_NODES_HPP

#include "common/types.hpp"
#include "dsl/templates.hpp"
#include <string>
#include <unordered_map>
#include <memory>
#include <concepts>
#include <optional>

namespace agenticflow {

// 节点基类
struct Node {
    NodeId id;
    NodeType type;
    std::optional<NodeId> next;
    std::optional<std::string> anchor;

    virtual ~Node() = default;
    virtual Context execute(Context& context) = 0;
};

// Start节点
struct StartNode : public Node {
    StartNode(std::string id, std::optional<NodeId> next_node = std::nullopt)
        : Node{.id = std::move(id), .type = NodeType::START, .next = next_node} {}

    Context execute(Context& context) override {
        return context; // Start节点只是跳转
    }
};

// End节点
struct EndNode : public Node {
    EndNode(std::string id)
        : Node{.id = std::move(id), .type = NodeType::END} {}

    Context execute(Context& context) override {
        return context; // End节点返回当前上下文
    }
};

// Set节点 - 使用 inja 渲染
struct SetNode : public Node {
    std::unordered_map<std::string, std::string> assign; // key -> template_string

    SetNode(std::string id,
            std::unordered_map<std::string, std::string> assigns,
            std::optional<NodeId> next_node = std::nullopt)
        : Node{.id = std::move(id), .type = NodeType::SET, .next = next_node}, assign(std::move(assigns)) {}

    Context execute(Context& context) override {
        Context new_context = context; // 复制上下文

        for (const auto& [key, template_str] : assign) {
            // 使用 inja 渲染模板
            std::string rendered_value = InjaTemplateRenderer::render(template_str, context);
            new_context[key] = rendered_value;
        }

        return new_context;
    }
};

// LLM调用节点
struct LLMCallNode : public Node {
    std::string prompt_template;
    std::string output_key;

    LLMCallNode(std::string id,
                std::string prompt,
                std::string output_key,
                std::optional<NodeId> next_node = std::nullopt)
        : Node{.id = std::move(id), .type = NodeType::LLM_CALL, .next = next_node},
          prompt_template(std::move(prompt)), output_key(std::move(output_key)) {}

    Context execute(Context& context) override {
        Context new_context = context;

        // 使用 inja 渲染提示模板
        std::string rendered_prompt = InjaTemplateRenderer::render(prompt_template, context);

        // 这里调用LLM（Phase 1 用模拟）
        std::string llm_response = "[MOCK] Generated response for: " + rendered_prompt.substr(0, 50);

        new_context[output_key] = llm_response;
        return new_context;
    }
};

// 工具调用节点
struct ToolCallNode : public Node {
    std::string tool_name;
    std::unordered_map<std::string, std::string> args; // arg_name -> template_string
    std::string output_key;

    ToolCallNode(std::string id,
                 std::string tool,
                 std::unordered_map<std::string, std::string> arguments,
                 std::string output_key,
                 std::optional<NodeId> next_node = std::nullopt)
        : Node{.id = std::move(id), .type = NodeType::TOOL_CALL, .next = next_node},
          tool_name(std::move(tool)), args(std::move(arguments)), output_key(std::move(output_key)) {}

    Context execute(Context& context) override {
        Context new_context = context;

        // 渲染参数
        std::unordered_map<std::string, std::string> rendered_args;
        for (const auto& [key, template_str] : args) {
            rendered_args[key] = InjaTemplateRenderer::render(template_str, context);
        }

        // 调用工具
        auto registry = &ToolRegistry::instance();
        if (!registry->has_tool(tool_name)) {
             throw std::runtime_error("Tool '" + tool_name + "' not registered");
        }

        nlohmann::json result = registry->call_tool(tool_name, rendered_args);
        new_context[output_key] = result;

        return new_context;
    }
};

} // namespace agenticflow

#endif
```

### 4. 工具注册器 (`include/agenticflow/tools/registry.hpp`)

```cpp
#ifndef AGENFLOW_REGISTRY_HPP
#define AGENFLOW_REGISTRY_HPP

#include "common/types.hpp"
#include <unordered_map>
#include <functional>
#include <string_view>
#include <vector>

namespace agenticflow {

class ToolRegistry {
public:
    static ToolRegistry& instance() {
        static ToolRegistry registry;
        return registry;
    }

    template<typename Func>
    requires std::invocable<Func, const std::unordered_map<std::string, std::string>&>
    void register_tool(std::string_view name, Func&& func) {
        tools_[std::string(name)] = [func = std::forward<Func>(func)](const std::unordered_map<std::string, std::string>& args) -> nlohmann::json {
            return func(args);
        };
    }

    bool has_tool(std::string_view name) const {
        return tools_.find(std::string(name)) != tools_.end();
    }

    nlohmann::json call_tool(std::string_view name, const std::unordered_map<std::string, std::string>& args) {
        auto it = tools_.find(std::string(name));
        if (it == tools_.end()) {
            throw std::runtime_error("Tool '" + std::string(name) + "' not found");
        }
        return it->second(args);
    }

    std::vector<std::string> list_tools() const {
        std::vector<std::string> names;
        names.reserve(tools_.size());
        for (const auto& [name, _] : tools_) {
            names.push_back(name);
        }
        return names;
    }

private:
    ToolRegistry() = default;
    std::unordered_map<std::string, std::function<nlohmann::json(const std::unordered_map<std::string, std::string>&)>> tools_;

    // 预注册示例工具
    static bool register_default_tools() {
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

        return true;
    }

    // 静态初始化
    static bool init_default_tools;
};

bool ToolRegistry::init_default_tools = ToolRegistry::register_default_tools();

} // namespace agenticflow

#endif
```

### 5. 主引擎 (`include/agenticflow/core/engine.hpp`)

```cpp
#ifndef AGENFLOW_ENGINE_HPP
#define AGENFLOW_ENGINE_HPP

#include "executor.hpp"
#include "parser.hpp"
#include "llm/llama_adapter.hpp"
#include <memory>
#include <concepts>
#include <fstream>
#include <sstream>

namespace agenticflow {

class ModernAgenticFlow {
public:
    static std::unique_ptr<ModernAgenticFlow> from_markdown(const std::string& markdown_content,
                                                           const Context& initial_context = Context{}) {
        ModernMarkdownParser parser;
        auto graphs = parser.parse_from_string(markdown_content);

        if (graphs.empty()) {
            throw std::runtime_error("No valid graphs found in markdown content");
        }

        auto engine = std::make_unique<ModernAgenticFlow>(std::move(graphs[0].nodes));

        // 初始化LLM适配器
        LlamaAdapter::Config config;
        config.model_path = "models/ggml-model-f16.gguf";
        engine->llama_adapter_ = std::make_unique<LlamaAdapter>(config);

        return engine;
    }

    static std::unique_ptr<ModernAgenticFlow> from_file(const std::string& file_path,
                                                       const Context& initial_context = Context{}) {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + file_path);
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        return from_markdown(content, initial_context);
    }

    Context run(const Context& context = Context{}) {
        if (!executor_) {
            throw std::runtime_error("Executor not initialized");
        }
        return executor_->execute(context);
    }

    template<typename Func>
    requires std::invocable<Func, const std::unordered_map<std::string, std::string>&>
    void register_tool(std::string_view name, Func&& func) {
        ToolRegistry::instance().register_tool(name, std::forward<Func>(func));
    }

private:
    std::unique_ptr<ModernFlowExecutor> executor_;
    std::unique_ptr<LlamaAdapter> llama_adapter_;

    ModernAgenticFlow(std::vector<std::unique_ptr<Node>> nodes)
        : executor_(std::make_unique<ModernFlowExecutor>(std::move(nodes))) {}
};

} // namespace agenticflow

#endif
```

### 6. 更新的CMakeLists.txt (使用inja)

```cmake
cmake_minimum_required(VERSION 3.20)
project(agenticflow-cpp20-inja VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# llama.cpp
add_subdirectory(llama.cpp)
set(LLAMA_LIB llama)

# inja 和 nlohmann/json
include(FetchContent)

# nlohmann/json (inja 依赖)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3 # 确保版本兼容 inja
)
FetchContent_MakeAvailable(nlohmann_json)

# inja
FetchContent_Declare(
    inja
    GIT_REPOSITORY https://github.com/pantor/inja.git
    GIT_TAG v3.4.0 # 或者最新的稳定版本
)
FetchContent_MakeAvailable(inja)

# 包含目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 源文件
file(GLOB_RECURSE SOURCES "src/*.cpp")

# 创建库
add_library(agenticflow-cpp20-inja ${SOURCES})
target_link_libraries(agenticflow-cpp20-inja
    ${LLAMA_LIB}
    nlohmann_json::nlohmann_json # inja 会自动链接
    inja # 如果 FetchContent_MakeAvailable 提供了 target
    Threads::Threads
)

# 如果 inja 没有提供 cmake target，可能需要手动链接其依赖
# target_include_directories(agenticflow-cpp20-inja PRIVATE ${inja_SOURCE_DIR}/include)

target_include_directories(agenticflow-cpp20-inja PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(agenticflow-cpp20-inja PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-O3 -march=native -mtune=native>
    $<$<CXX_COMPILER_ID:MSVC>:/O2>
)

add_executable(agflow_modern_example examples/modern_cpp_examples.cpp)
target_link_libraries(agflow_modern_example agenticflow-cpp20-inja)

enable_testing()
add_executable(test_modern tests/test_modern.cpp)
target_link_libraries(test_modern agenticflow-cpp20-inja)
add_test(NAME test_modern COMMAND test_modern)

install(TARGETS agenticflow-cpp20-inja
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/ DESTINATION include)
```

### 7. 示例使用 (`examples/modern_cpp_examples.cpp`) - 展示inja功能

```cpp
#include "agenticflow/core/engine.hpp"
#include <iostream>
#include <format>

int main() {
    // 使用inja的复杂模板功能
    std::string markdown_content = R"(
# Modern Weather Assistant with Complex Templates

```yaml
nodes:
  - id: start
    type: start
    next: prepare_context

  - id: prepare_context
    type: set
    assign:
      location: "{{ user_input }}"
      weather_request: "get_weather(location='{{ location }}')"
      user_context: |
        {% if user_preferences %}
        User prefers: {{ user_preferences.temperature_unit }}
        {% endif %}
        {% if user_history %}
        Previous locations: {{ join(user_history.locations, ", ") }}
        {% endif %}
    next: call_weather_api

  - id: call_weather_api
    type: tool_call
    tool: get_weather
    args:
      location: "{{ location }}"
    output_key: weather_data
    next: generate_response

  - id: generate_response
    type: llm_call
    prompt_template: |
      {% if user_context %}
      User Context:
      {{ user_context }}
      {% endif %}

      Current weather in {{ location }}: {{ weather_data }}

      {% if examples %}
      Here are some example responses:
      {% for example in examples %}
      - Query: {{ example.query }}
        Response: {{ example.response }}
      {% endfor %}
      {% endif %}

      Provide a friendly and informative weather update.
    output_key: final_response
    next: end

  - id: end
    type: end
```
)";

    try {
        auto engine = agenticflow::ModernAgenticFlow::from_markdown(markdown_content);

        // 构建包含复杂结构的上下文
        agenticflow::Context context;
        context["user_input"] = "Beijing";
        context["user_preferences"]["temperature_unit"] = "Celsius";
        context["user_history"]["locations"] = {"Shanghai", "Guangzhou"};
        context["examples"] = nlohmann::json::array();
        context["examples"].push_back({
            {"query", "What's the weather like today?"},
            {"response", "It's sunny and warm."}
        });
        context["examples"].push_back({
            {"query", "Is it going to rain?"},
            {"response", "No rain expected today."}
        });

        auto result = engine->run(context);

        if (result.contains("final_response")) {
            std::cout << std::format("Final response: {}\n", result["final_response"].get<std::string>());
        }

    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }

    return 0;
}
```

## 总结

使用 `inja` 重构后，AgenticFlow获得了以下优势：

1.  **强大的模板功能**: 现在可以支持条件 (`{% if %}`)、循环 (`{% for %}`)、内置函数 (`join`, `length`, `upper` 等) 和更复杂的模板逻辑，非常适合构建动态的 LLM 提示。
2.  **与 JSON 集成**: `inja` 原生使用 `nlohmann::json`，与我们的上下文数据结构完美契合。
3.  **灵活性**: 模板编写者可以利用 `inja` 的全部功能来构建复杂的、动态的内容。
4.  **性能权衡**: 虽然 `inja` 的性能低于 `fmt`，但在 LLM 推理通常是瓶颈的场景下，模板渲染的开销通常是可接受的。

这个实现充分利用了 `inja` 的能力，使您的 DSL 更加强大和灵活。
