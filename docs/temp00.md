您说得对！使用C++20标准和编译期特性，以及选择更适合C++的模板库，会让实现更加高效和现代。让我重新设计架构，充分利用C++20的特性。

## 现代C++20 AgenticFlow架构

```
agenticflow-cpp20/
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

### 1. 现代类型定义 (`include/common/types.hpp`)

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
#include <span>
#include <source_location>

namespace agenticflow {

// 使用C++20概念定义可转换为字符串的类型
template<typename T>
concept StringConvertible = std::convertible_to<T, std::string> || 
                           std::same_as<T, std::string_view> ||
                           std::same_as<T, const char*>;

// 值类型定义 - 使用更现代的类型
using Value = std::variant<std::string, int, double, bool, std::monostate, 
                          std::unordered_map<std::string, std::any>,
                          std::vector<std::any>>;

// 编译期字符串常量
template<size_t N>
struct FixedString {
    char data[N] = {};
    constexpr FixedString(const char (&str)[N]) {
        std::copy_n(str, N, data);
    }
    constexpr operator std::string_view() const { return {data, N-1}; }
};

// 编译期节点ID
template<FixedString id>
struct NodeId {
    static constexpr std::string_view value = id;
    constexpr operator std::string_view() const { return value; }
};

// 节点类型枚举
enum class NodeType : uint8_t {
    START,
    END,
    SET,
    LLM_CALL,
    TOOL_CALL
};

// 上下文类型 - 使用更高效的存储
using Context = std::unordered_map<std::string, std::any>;

// 工具函数类型
using ToolFunction = std::function<std::any(const std::unordered_map<std::string, std::string>&)>;

} // namespace agenticflow

#endif
```

### 2. 编译期模板解析 (`include/agenticflow/dsl/templates.hpp`)

```cpp
#ifndef AGENFLOW_TEMPLATES_HPP
#define AGENFLOW_TEMPLATES_HPP

#include "common/types.hpp"
#include <string>
#include <string_view>
#include <array>
#include <tuple>
#include <ranges>
#include <format>  // C++20 format
#include <regex>

namespace agenticflow {

// 编译期模板解析器
class CompileTimeTemplateParser {
public:
    // 编译期解析模板，生成解析结果
    template<size_t N>
    static constexpr auto parse_template(const char (&template_str)[N]) {
        // 在编译期解析模板，找出所有变量引用
        std::array<std::string_view, 16> variables{}; // 预分配16个变量槽
        size_t count = 0;
        
        // 使用C++20 ranges进行编译期解析
        std::string_view tpl{template_str, N-1};
        size_t pos = 0;
        
        while (pos < tpl.size() && count < 16) {
            size_t start = tpl.find("{{", pos);
            if (start == std::string_view::npos) break;
            
            size_t end = tpl.find("}}", start);
            if (end == std::string_view::npos) break;
            
            variables[count++] = tpl.substr(start + 2, end - start - 2);
            pos = end + 2;
        }
        
        return std::make_pair(variables, count);
    }
};

// 运行时模板渲染器
class RuntimeTemplateRenderer {
public:
    // 使用C++20 std::format进行高效渲染
    static std::string render(std::string_view template_str, const Context& context) {
        std::string result = std::string(template_str);
        
        // 查找并替换变量
        size_t pos = 0;
        while ((pos = result.find("{{", pos)) != std::string::npos) {
            size_t end = result.find("}}", pos);
            if (end == std::string::npos) break;
            
            std::string var_name = result.substr(pos + 2, end - pos - 2);
            
            // 查找变量值
            auto it = context.find(var_name);
            std::string replacement = (it != context.end()) ? 
                std::any_cast<std::string>(it->second) : "";
            
            result.replace(pos, end - pos + 2, replacement);
            pos = pos + replacement.length();
        }
        
        return result;
    }
    
    // 编译期优化版本 - 如果模板在编译期已知
    template<FixedString TemplateStr>
    static std::string render_optimized(const Context& context) {
        constexpr auto [variables, count] = CompileTimeTemplateParser::parse_template(TemplateStr);
        
        std::string result = std::string(TemplateStr.value);
        
        for (size_t i = 0; i < count; ++i) {
            std::string_view var_name = variables[i];
            std::string var_pattern = "{{" + std::string(var_name) + "}}";
            
            auto it = context.find(std::string(var_name));
            std::string replacement = (it != context.end()) ? 
                std::any_cast<std::string>(it->second) : "";
            
            size_t pos = result.find(var_pattern);
            if (pos != std::string::npos) {
                result.replace(pos, var_pattern.length(), replacement);
            }
        }
        
        return result;
    }
};

// 使用fmt库的高性能模板渲染（推荐）
class FmtTemplateRenderer {
public:
    static std::string render(std::string_view format_str, const Context& context) {
        // 这里可以使用fmt库进行更高效的格式化
        // 由于fmt库的集成，这里展示概念
        return RuntimeTemplateRenderer::render(format_str, context);
    }
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
#include <ranges>

namespace agenticflow {

// 节点基类 - 使用CRTP进行编译期优化
template<typename Derived>
struct NodeBase {
    std::string id;
    NodeType type;
    std::optional<std::string> next;
    std::optional<std::string> anchor;
    
    virtual ~NodeBase() = default;
    virtual Context execute(Context& context) = 0;
    
    // 类型安全的动态转换
    template<typename T>
    T* as() { return dynamic_cast<T*>(this); }
    
    template<typename T>
    const T* as() const { return dynamic_cast<const T*>(this); }
};

// Start节点
struct StartNode : public NodeBase<StartNode> {
    StartNode(std::string id, std::optional<std::string> next_node = std::nullopt)
        : NodeBase<StartNode>{.id = std::move(id), .type = NodeType::START, .next = next_node} {}
    
    Context execute(Context& context) override {
        return context; // Start节点只是跳转
    }
};

// End节点
struct EndNode : public NodeBase<EndNode> {
    EndNode(std::string id)
        : NodeBase<EndNode>{.id = std::move(id), .type = NodeType::END} {}
    
    Context execute(Context& context) override {
        return context; // End节点返回当前上下文
    }
};

// Set节点 - 支持编译期优化
struct SetNode : public NodeBase<SetNode> {
    std::unordered_map<std::string, std::string> assign;
    
    SetNode(std::string id, 
            std::unordered_map<std::string, std::string> assigns,
            std::optional<std::string> next_node = std::nullopt)
        : NodeBase<SetNode>{.id = std::move(id), .type = NodeType::SET, .next = next_node}, 
          assign(std::move(assigns)) {}
    
    Context execute(Context& context) override {
        Context new_context = context;
        
        for (const auto& [key, template_str] : assign) {
            std::string rendered_value = RuntimeTemplateRenderer::render(template_str, context);
            new_context[key] = std::move(rendered_value);
        }
        
        return new_context;
    }
};

// 编译期优化的Set节点
template<FixedString... Assignments>
struct CompileTimeSetNode : public NodeBase<CompileTimeSetNode<Assignments...>> {
    std::array<std::pair<std::string_view, std::string_view>, sizeof...(Assignments)> assignments;
    
    CompileTimeSetNode(std::string id, std::optional<std::string> next_node = std::nullopt) 
        : NodeBase<CompileTimeSetNode<Assignments...>>{
            .id = std::move(id), 
            .type = NodeType::SET, 
            .next = next_node
        } {
        // 在构造时初始化编译期解析的赋值
        assignments = parse_assignments();
    }
    
    Context execute(Context& context) override {
        Context new_context = context;
        
        for (const auto& [key, template_str] : assignments) {
            std::string rendered_value = RuntimeTemplateRenderer::render(template_str, context);
            new_context[std::string(key)] = std::move(rendered_value);
        }
        
        return new_context;
    }
    
private:
    constexpr auto parse_assignments() {
        std::array<std::pair<std::string_view, std::string_view>, sizeof...(Assignments)> result{};
        // 编译期解析赋值表达式
        return result;
    }
};

// LLM调用节点
struct LLMCallNode : public NodeBase<LLMCallNode> {
    std::string prompt_template;
    std::string output_key;
    
    LLMCallNode(std::string id,
                std::string prompt,
                std::string output_key,
                std::optional<std::string> next_node = std::nullopt)
        : NodeBase<LLMCallNode>{
            .id = std::move(id), 
            .type = NodeType::LLM_CALL, 
            .next = next_node
        },
          prompt_template(std::move(prompt)), output_key(std::move(output_key)) {}
    
    Context execute(Context& context) override;
};

// 工具调用节点
struct ToolCallNode : public NodeBase<ToolCallNode> {
    std::string tool_name;
    std::unordered_map<std::string, std::string> args;
    std::string output_key;
    
    ToolCallNode(std::string id,
                 std::string tool,
                 std::unordered_map<std::string, std::string> arguments,
                 std::string output_key,
                 std::optional<std::string> next_node = std::nullopt)
        : NodeBase<ToolCallNode>{
            .id = std::move(id), 
            .type = NodeType::TOOL_CALL, 
            .next = next_node
        },
          tool_name(std::move(tool)), args(std::move(arguments)), output_key(std::move(output_key)) {}
    
    Context execute(Context& context) override;
};

} // namespace agenticflow

#endif
```

### 4. 现代工具注册器 (`include/agenticflow/tools/registry.hpp`)

```cpp
#ifndef AGENFLOW_REGISTRY_HPP
#define AGENFLOW_REGISTRY_HPP

#include "common/types.hpp"
#include <unordered_map>
#include <functional>
#include <string_view>
#include <span>
#include <concepts>

namespace agenticflow {

class ToolRegistry {
public:
    static ToolRegistry& instance() {
        static ToolRegistry registry;
        return registry;
    }
    
    // 使用概念约束的工具注册
    template<typename Func>
    requires std::invocable<Func, const std::unordered_map<std::string, std::string>&>
    void register_tool(std::string_view name, Func&& func) {
        tools_[std::string(name)] = std::forward<Func>(func);
    }
    
    bool has_tool(std::string_view name) const {
        return tools_.find(std::string(name)) != tools_.end();
    }
    
    std::any call_tool(std::string_view name, const std::unordered_map<std::string, std::string>& args) {
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
    std::unordered_map<std::string, ToolFunction> tools_;
};

// 编译期工具注册宏
#define REGISTER_TOOL_CPP20(name, func) \
    []() { \
        static bool registered = []() { \
            agenticflow::ToolRegistry::instance().register_tool(name, func); \
            return true; \
        }(); \
        return registered; \
    }()

} // namespace agenticflow

#endif
```

### 5. 现代解析器 (`include/agenticflow/core/parser.hpp`)

```cpp
#ifndef AGENFLOW_PARSER_HPP
#define AGENFLOW_PARSER_HPP

#include "nodes.hpp"
#include <string>
#include <vector>
#include <memory>
#include <span>
#include <ranges>
#include <charconv>
#include <simdjson.h>  // 使用simdjson进行高性能JSON解析

namespace agenticflow {

struct ParsedGraph {
    std::vector<std::unique_ptr<NodeBase<NodeBase>>> nodes;
    std::optional<std::string> anchor;
    std::string section_title;
};

class ModernMarkdownParser {
public:
    std::vector<ParsedGraph> parse_from_string(std::string_view markdown_content) {
        // 使用C++20 ranges进行高效解析
        auto sections = split_markdown_sections(markdown_content);
        std::vector<ParsedGraph> graphs;
        
        for (auto& section : sections) {
            auto yaml_blocks = extract_yaml_blocks(section.content);
            for (auto& yaml_content : yaml_blocks) {
                auto graph = parse_yaml_to_graph(yaml_content);
                if (!graph.nodes.empty()) {
                    graph.anchor = section.anchor;
                    graph.section_title = section.title;
                    graphs.push_back(std::move(graph));
                }
            }
        }
        
        return graphs;
    }
    
    std::vector<ParsedGraph> parse_from_file(const std::string& file_path) {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file: " + file_path);
        }
        
        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        
        return parse_from_string(content);
    }

private:
    struct Section {
        std::string title;
        std::string content;
        std::optional<std::string> anchor;
    };
    
    std::vector<Section> split_markdown_sections(std::string_view content) {
        std::vector<Section> sections;
        
        auto lines = content | std::views::split('\n') 
                           | std::views::transform([](auto&& rng) {
                               return std::string(rng.begin(), rng.end());
                           });
        
        Section current_section;
        for (const auto& line : lines) {
            if (line.starts_with("# ")) {
                if (!current_section.content.empty()) {
                    sections.push_back(current_section);
                }
                current_section = parse_title_line(line);
            } else {
                current_section.content += line + "\n";
            }
        }
        
        if (!current_section.content.empty()) {
            sections.push_back(current_section);
        }
        
        return sections;
    }
    
    std::vector<std::string> extract_yaml_blocks(std::string_view content) {
        std::vector<std::string> blocks;
        std::string str_content(content);
        
        size_t pos = 0;
        while ((pos = str_content.find("```yaml", pos)) != std::string::npos) {
            size_t start = pos;
            pos += 7; // length of "```yaml"
            
            size_t end = str_content.find("```", pos);
            if (end == std::string::npos) break;
            
            std::string yaml_block = str_content.substr(pos, end - pos);
            blocks.push_back(std::move(yaml_block));
            pos = end + 3;
        }
        
        return blocks;
    }
    
    ParsedGraph parse_yaml_to_graph(std::string_view yaml_content) {
        ParsedGraph graph;
        
        // 使用simdjson解析JSON/YAML
        simdjson::dom::parser parser;
        simdjson::dom::element doc;
        
        auto error = parser.parse(yaml_content.data(), yaml_content.size()).get(doc);
        if (error) {
            throw std::runtime_error("Failed to parse YAML: " + std::to_string(error));
        }
        
        // 解析节点
        if (doc.at_key("nodes").error() == simdjson::SUCCESS) {
            auto nodes_array = doc["nodes"].get_array();
            for (auto node_element : nodes_array) {
                auto node = create_node_from_json(node_element);
                if (node) {
                    graph.nodes.push_back(std::move(node));
                }
            }
        }
        
        return graph;
    }
    
    std::unique_ptr<NodeBase<NodeBase>> create_node_from_json(const simdjson::dom::element& node_json) {
        std::string type_str;
        node_json["type"].get(std::string_view(type_str));
        
        NodeType node_type;
        if (type_str == "start") node_type = NodeType::START;
        else if (type_str == "end") node_type = NodeType::END;
        else if (type_str == "set") node_type = NodeType::SET;
        else if (type_str == "llm_call") node_type = NodeType::LLM_CALL;
        else if (type_str == "tool_call") node_type = NodeType::TOOL_CALL;
        else {
            throw std::runtime_error("Unknown node type: " + type_str);
        }
        
        std::string id;
        node_json["id"].get(std::string_view(id));
        
        std::optional<std::string> next;
        if (node_json.at_key("next").error() == simdjson::SUCCESS) {
            std::string next_str;
            node_json["next"].get(std::string_view(next_str));
            next = next_str;
        }
        
        switch (node_type) {
            case NodeType::START:
                return std::make_unique<StartNode>(std::move(id), next);
            case NodeType::END:
                return std::make_unique<EndNode>(std::move(id));
            case NodeType::SET: {
                auto assign_obj = node_json["assign"].get_object();
                std::unordered_map<std::string, std::string> assign;
                for (auto [key, value] : assign_obj) {
                    std::string val_str;
                    value.get(std::string_view(val_str));
                    assign[std::string(key)] = val_str;
                }
                return std::make_unique<SetNode>(std::move(id), std::move(assign), next);
            }
            case NodeType::LLM_CALL: {
                std::string prompt, output_key;
                node_json["prompt_template"].get(std::string_view(prompt));
                node_json["output_key"].get(std::string_view(output_key));
                return std::make_unique<LLMCallNode>(std::move(id), std::move(prompt), std::move(output_key), next);
            }
            case NodeType::TOOL_CALL: {
                std::string tool, output_key;
                node_json["tool"].get(std::string_view(tool));
                node_json["output_key"].get(std::string_view(output_key));
                
                auto args_obj = node_json["args"].get_object();
                std::unordered_map<std::string, std::string> args;
                for (auto [key, value] : args_obj) {
                    std::string val_str;
                    value.get(std::string_view(val_str));
                    args[std::string(key)] = val_str;
                }
                
                return std::make_unique<ToolCallNode>(std::move(id), std::move(tool), 
                                                     std::move(args), std::move(output_key), next);
            }
        }
        
        return nullptr;
    }
    
    Section parse_title_line(const std::string& line) {
        Section section;
        size_t anchor_pos = line.find("{#");
        if (anchor_pos != std::string::npos) {
            size_t anchor_end = line.find("}", anchor_pos);
            if (anchor_end != std::string::npos) {
                section.anchor = line.substr(anchor_pos + 2, anchor_end - anchor_pos - 2);
                section.title = line.substr(0, anchor_pos);
            }
        } else {
            section.title = line;
        }
        
        // 移除标题标记
        if (section.title.starts_with("# ")) {
            section.title = section.title.substr(2);
        } else if (section.title.starts_with("## ")) {
            section.title = section.title.substr(3);
        }
        
        return section;
    }
};

} // namespace agenticflow

#endif
```

### 6. 现代执行器 (`include/agenticflow/core/executor.hpp`)

```cpp
#ifndef AGENFLOW_EXECUTOR_HPP
#define AGENFLOW_EXECUTOR_HPP

#include "nodes.hpp"
#include <vector>
#include <unordered_map>
#include <memory>
#include <execution>
#include <future>
#include <ranges>

namespace agenticflow {

class ModernFlowExecutor {
public:
    ModernFlowExecutor(std::vector<std::unique_ptr<NodeBase<NodeBase>>> nodes)
        : nodes_(std::move(nodes)) {
        // 构建节点映射表
        for (const auto& node : nodes_) {
            node_map_[node->id] = node.get();
        }
    }
    
    Context execute(const Context& initial_context = Context{}) {
        Context context = initial_context;
        
        // 找到起始节点
        NodeBase<NodeBase>* current_node = find_start_node();
        if (!current_node) {
            throw std::runtime_error("No start node found");
        }
        
        size_t step_count = 0;
        const size_t max_steps = 100;
        
        while (current_node && step_count < max_steps) {
            context = current_node->execute(context);
            
            // 如果是End节点，退出
            if (current_node->type == NodeType::END) {
                break;
            }
            
            // 获取下一个节点
            if (!current_node->next) {
                break; // 没有下一个节点
            }
            
            auto next_it = node_map_.find(*current_node->next);
            if (next_it == node_map_.end()) {
                throw std::runtime_error("Next node '" + *current_node->next + "' not found");
            }
            
            current_node = next_it->second;
            ++step_count;
        }
        
        if (step_count >= max_steps) {
            throw std::runtime_error("Maximum execution steps exceeded");
        }
        
        return context;
    }

private:
    std::vector<std::unique_ptr<NodeBase<NodeBase>>> nodes_;
    std::unordered_map<std::string, NodeBase<NodeBase>*> node_map_;
    
    NodeBase<NodeBase>* find_start_node() const {
        for (const auto& node : nodes_) {
            if (node->type == NodeType::START) {
                return node.get();
            }
        }
        return nullptr;
    }
};

} // namespace agenticflow

#endif
```

### 7. 现代主引擎 (`include/agenticflow/core/engine.hpp`)

```cpp
#ifndef AGENFLOW_ENGINE_HPP
#define AGENFLOW_ENGINE_HPP

#include "executor.hpp"
#include "parser.hpp"
#include "llm/llama_adapter.hpp"
#include <memory>
#include <concepts>

namespace agenticflow {

class ModernAgenticFlow {
public:
    template<StringConvertible Source>
    static std::unique_ptr<ModernAgenticFlow> from_source(Source&& source, 
                                                         const Context& initial_context = Context{}) {
        ModernMarkdownParser parser;
        auto graphs = parser.parse_from_string(std::forward<Source>(source));
        
        if (graphs.empty()) {
            throw std::runtime_error("No valid graphs found in source");
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
        ModernMarkdownParser parser;
        auto graphs = parser.parse_from_file(file_path);
        
        if (graphs.empty()) {
            throw std::runtime_error("No valid graphs found in file: " + file_path);
        }
        
        auto engine = std::make_unique<ModernAgenticFlow>(std::move(graphs[0].nodes));
        
        // 初始化LLM适配器
        LlamaAdapter::Config config;
        config.model_path = "models/ggml-model-f16.gguf";
        engine->llama_adapter_ = std::make_unique<LlamaAdapter>(config);
        
        return engine;
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
    
    ModernAgenticFlow(std::vector<std::unique_ptr<NodeBase<NodeBase>>> nodes)
        : executor_(std::make_unique<ModernFlowExecutor>(std::move(nodes))) {}
};

} // namespace agenticflow

#endif
```

### 8. 更新的CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(agenticflow-cpp20 VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 启用C++20特性
set(CMAKE_CXX_EXTENSIONS OFF)

# 查找依赖
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# llama.cpp
add_subdirectory(llama.cpp)
set(LLAMA_LIB llama)

# simdjson for fast JSON parsing
include(FetchContent)
FetchContent_Declare(
    simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG v3.11.0
)
FetchContent_MakeAvailable(simdjson)

# fmt for modern formatting (替代Jinja2)
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 10.2.1
)
FetchContent_MakeAvailable(fmt)

# 包含目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 源文件
file(GLOB_RECURSE SOURCES "src/*.cpp")

# 创建库
add_library(agenticflow-cpp20 ${SOURCES})
target_link_libraries(agenticflow-cpp20 
    ${LLAMA_LIB}
    simdjson::simdjson
    fmt::fmt
    Threads::Threads
)

target_include_directories(agenticflow-cpp20 PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 启用编译器优化
target_compile_options(agenticflow-cpp20 PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-O3 -march=native -mtune=native>
    $<$<CXX_COMPILER_ID:MSVC>:/O2>
)

# 示例程序
add_executable(agflow_modern_example examples/modern_cpp_examples.cpp)
target_link_libraries(agflow_modern_example agenticflow-cpp20)

# 测试
enable_testing()
add_executable(test_modern tests/test_modern.cpp)
target_link_libraries(test_modern agenticflow-cpp20)
add_test(NAME test_modern COMMAND test_modern)

# 安装规则
install(TARGETS agenticflow-cpp20
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/ DESTINATION include)
```

### 9. 现代示例 (`examples/modern_cpp_examples.cpp`)

```cpp
#include "agenticflow/core/engine.hpp"
#include <iostream>
#include <format>

int main() {
    std::string markdown_content = R"(
# Modern Weather Assistant

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
        auto engine = agenticflow::ModernAgenticFlow::from_source(markdown_content);
        
        agenticflow::Context context = {
            {"user_input", std::string("Beijing")}
        };
        
        auto result = engine->run(context);
        
        if (auto it = result.find("weather_data"); it != result.end()) {
            std::cout << std::format("Weather result: {}\n", 
                                   std::any_cast<std::string>(it->second));
        }
        
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }
    
    return 0;
}
```

## C++20版本的优势

### 1. **编译期优化**
- `constexpr` 和 `consteval` 函数
- 编译期模板解析
- 概念（Concepts）提供更好的类型约束

### 2. **现代模板库选择**
- **fmt库**: 比Jinja2更快更安全的格式化
- **simdjson**: 高性能JSON解析
- **std::format**: C++20标准格式化

### 3. **性能提升**
- 零开销抽象
- 编译期计算
- 更好的内存布局

### 4. **类型安全**
- 强类型概念
- 编译期检查
- RAII和智能指针

这个现代C++20实现充分利用了最新的C++特性，提供了更好的性能、类型安全和开发体验。与llama.cpp的集成也更加自然高效。
