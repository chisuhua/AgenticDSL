# AgenticOS Layer 0: agentic-dsl-runtime 重构规范 v2.2

**文档版本：** v2.2.0  
**日期：** 2026-02-25  
**范围：** agentic-dsl-runtime（C++ 核心运行时）  
**状态：** 基于现有 AgenticDSL 代码库分析，面向 AgenticOS v2.2 架构的重构计划  
**依赖：** AgenticOS-Architecture-v2.2（`docs/AgenticOS_Architecture.md`）  
**基础代码：** `chisuhua/AgenticDSL`（`src/` 目录）

---

## 1. 核心定位

Layer 0（`agentic-dsl-runtime`）是 AgenticOS 的底层执行引擎，基于现有 `AgenticDSL` C++ 代码库重构而来。现有代码已实现以下核心能力：

| 能力 | 现有实现 | 重构目标 |
| :--- | :--- | :--- |
| DSL 解析 | `MarkdownParser`（Markdown 格式解析） | 保留，增加 Layer Profile 编译时验证 |
| 拓扑调度 | `TopoScheduler`（Kahn 算法） | 保留，增强智能调度（metadata.priority） |
| 节点执行 | `NodeExecutor`（纯函数式分发） | 保留，增加 state.read/write 工具支持 |
| 预算控制 | `BudgetController` + `ExecutionBudget`（原子计数器） | 保留，增加自适应预算继承 |
| 上下文管理 | `ContextEngine`（快照 + 合并策略） | 保留 |
| 工具注册 | `ToolRegistry` | 保留，增加 state 工具注册 |
| LLM 适配 | `LlamaAdapter`（本地 llama.cpp） | 扩展为多后端适配器接口 |
| 追踪导出 | `TraceExporter` | 保留 |
| 标准库加载 | `StandardLibraryLoader` | 扩展为 `/lib/cognitive/`, `/lib/thinking/`, `/lib/workflow/` 分层 |

**关键约束（继承自 AgenticOS Architecture v2.2）：**

* ✅ L0 是纯函数式运行时，节点执行不修改传入 AST/Context，通过返回值传递变更
* ✅ L0 禁止维护跨执行的会话状态（session state）
* ✅ L0 禁止在节点执行期间修改 AST 结构
* ✅ L3 禁止直接调用 `DSLEngine::run()`，必须经过 L2 调度器
* ✅ Python 仅作 Thin Wrapper（pybind11），业务逻辑必须 DSL 化

**技术栈：** C++20（当前 `CMakeLists.txt` 使用 `CMAKE_CXX_STANDARD 20`），CMake 3.20+，pybind11（计划中）  
**外部依赖：** llama.cpp, nlohmann/json, inja（模板渲染），yaml-cpp

---

## 2. 现有代码库结构分析

### 2.1 当前目录结构

```text
AgenticDSL/
├── src/
│   ├── core/
│   │   ├── engine.h / engine.cpp          # DSLEngine（主入口）
│   │   └── types/
│   │       ├── node.h                     # 节点类型定义（NodeType, Node 子类）
│   │       ├── budget.h                   # ExecutionBudget（原子计数器）
│   │       ├── context.h                  # Context（nlohmann::json 别名）
│   │       ├── resource.h                 # Resource, ResourceType
│   │       └── common.h
│   ├── common/
│   │   ├── llm/
│   │   │   ├── llama_adapter.h/cpp        # LlamaAdapter（本地 llama.cpp）
│   │   └── tools/
│   │       ├── registry.h/cpp             # ToolRegistry
│   │   └── utils/
│   │       ├── template_renderer.h/cpp    # Inja 模板渲染
│   │       ├── parser_utils.h/cpp
│   │       └── yaml_json.h/cpp
│   └── modules/
│       ├── parser/
│       │   ├── markdown_parser.h/cpp      # MarkdownParser（DSL 文档解析）
│       ├── scheduler/
│       │   ├── topo_scheduler.h/cpp       # TopoScheduler（Kahn 算法）
│       │   ├── execution_session.h/cpp    # ExecutionSession（单次执行封装）
│       │   └── resource_manager.h/cpp     # ResourceManager
│       ├── executor/
│       │   ├── node_executor.h/cpp        # NodeExecutor（节点分发）
│       │   └── node.cpp                   # 各节点类型执行实现
│       ├── budget/
│       │   └── budget_controller.h/cpp    # BudgetController
│       ├── context/
│       │   └── context_engine.h/cpp       # ContextEngine（快照 + 合并）
│       ├── trace/
│       │   └── trace_exporter.h/cpp       # TraceExporter
│       ├── library/
│       │   ├── library_loader.h/cpp       # StandardLibraryLoader
│       │   └── schema.h                   # LibraryEntry
│       └── system/
│           └── system_nodes.h/cpp         # 系统内置节点（预算超限等）
├── lib/                                   # 标准库 DSL 子图
│   ├── auth/
│   ├── human/
│   ├── math/
│   └── utils/
├── tests/                                 # 单元测试（Catch2）
├── examples/
│   ├── agent_basic/
│   ├── agent_loop/
│   └── agent_simple/
├── external/
│   ├── llama.cpp/
│   ├── nlohmann_json/
│   ├── inja/
│   └── yaml-cpp/
└── CMakeLists.txt
```

### 2.2 核心类关系

```text
DSLEngine
├── MarkdownParser              ← DSL Markdown 文档解析，输出 ParsedGraph
├── TopoScheduler               ← Kahn 算法拓扑调度，管理 DAG 执行
│   ├── ExecutionSession        ← 单次执行封装
│   │   ├── NodeExecutor        ← 节点类型分发执行
│   │   │   ├── ToolRegistry    ← 工具注册与调用
│   │   │   ├── LlamaAdapter    ← LLM 调用（llm_call / generate_subgraph）
│   │   │   └── MarkdownParser  ← 动态子图解析（generate_subgraph）
│   │   ├── BudgetController    ← 预算检查与消耗
│   │   ├── ContextEngine       ← 上下文快照与合并
│   │   └── TraceExporter       ← 执行轨迹记录
│   └── ResourceManager         ← 资源注册与访问
└── StandardLibraryLoader       ← 标准库子图加载（单例）
```

---

## 3. 节点类型系统

### 3.1 现有节点类型（`src/core/types/node.h`）

```cpp
enum class NodeType : uint8_t {
    START,              // 入口节点
    END,                // 出口节点
    ASSIGN,             // 变量赋值（支持 Inja 模板）
    LLM_CALL,           // LLM 推理调用
    TOOL_CALL,          // 工具调用（ToolRegistry）
    RESOURCE,           // 资源声明（文件/DB/API）
    FORK,               // 并行分支（v3.1）
    JOIN,               // 分支合并（v3.1）
    GENERATE_SUBGRAPH,  // 动态子图生成（llm_generate_dsl）
    ASSERT              // 条件断言
};
```

### 3.2 ParsedGraph 结构

```cpp
struct ParsedGraph {
    std::vector<std::unique_ptr<Node>> nodes;
    NodePath path;                           // e.g., /main
    nlohmann::json metadata;                 // 图级 metadata
    std::optional<ExecutionBudget> budget;   // 从 /__meta__ 解析
    std::optional<std::string> signature;   // 子图签名
    std::vector<std::string> permissions;   // 子图权限声明
    bool is_standard_library = false;        // 路径以 /lib/ 开头
    std::optional<nlohmann::json> output_schema; // 输出 JSON Schema
};
```

### 3.3 L0 重构：计划新增节点类型

| 节点类型 | 说明 | 优先级 |
| :--- | :--- | :--- |
| `CONDITION` | 条件分支（替代 ASSERT 中的跳转逻辑） | P1 |
| `STATE_READ` | `state.read` 工具调用（L4 状态读取） | P2（v2.2 新增） |
| `STATE_WRITE` | `state.write` 工具调用（L4 状态写入） | P2（v2.2 新增） |

---

## 4. 解析器模块

### 4.1 现有实现（`MarkdownParser`）

现有解析器以 **Markdown 格式**解析 DSL 文档，输出 `ParsedGraph` 列表。

```cpp
class MarkdownParser {
public:
    // 从字符串解析，返回多个 ParsedGraph（支持多图）
    std::vector<ParsedGraph> parse_from_string(const std::string& markdown_content);
    
    // 从文件路径解析
    std::vector<ParsedGraph> parse_from_file(const std::string& file_path);
    
    // 从 JSON 对象创建单个节点
    std::unique_ptr<Node> create_node_from_json(const NodePath& path, const nlohmann::json& node_json);
    
private:
    void validate_nodes(const std::vector<std::unique_ptr<Node>>& nodes);
    std::optional<nlohmann::json> parse_output_schema_from_signature(const std::string& signature_str);
};
```

### 4.2 重构计划：增加编译时验证

在 `MarkdownParser` 或独立的 `SemanticValidator` 中增加：

```cpp
// 计划新增（Phase 5：智能化演进）
class SemanticValidator {
public:
    explicit SemanticValidator(const std::vector<ParsedGraph>& graphs);
    void validate();
    
private:
    void validate_layer_profile();           // Layer Profile 与命名空间匹配
    void validate_state_tool_compatibility(); // state.read/write 权限声明
    void validate_node_references();          // 节点引用有效性
    void detect_cycles();                      // 环检测
};
```

**Layer Profile 规则（v2.2 计划）：**

| 命名空间前缀 | 要求 Profile | 约束 |
| :--- | :--- | :--- |
| `/lib/cognitive/**` | `Cognitive` | 仅允许 `state.read`/`state.write` 工具调用 |
| `/lib/thinking/**` | `Thinking` | 禁止 `state.write` |
| `/lib/workflow/**` | `Workflow` | 无额外约束 |
| `/dynamic/**` | 继承父图 | 运行时动态生成 |

---

## 5. 调度器模块

### 5.1 现有实现（`TopoScheduler`）

```cpp
class TopoScheduler {
public:
    struct Config {
        std::optional<ExecutionBudget> initial_budget;
    };

    TopoScheduler(Config config, ToolRegistry& tool_registry, 
                  LlamaAdapter* llm_adapter, 
                  const std::vector<ParsedGraph>* full_graphs = nullptr);

    void register_node(std::unique_ptr<Node> node);
    void build_dag();           // 构建 DAG（计算入度 + 反向边）
    ExecutionResult execute(Context initial_context);  // Kahn 算法执行

    // 动态子图追加（generate_subgraph 回调）
    void append_dynamic_graphs(std::vector<ParsedGraph> new_graphs);
    
    std::vector<TraceRecord> get_last_traces() const;
};
```

**现有 Fork/Join 支持：**

`TopoScheduler` 已实现 Fork/Join 模拟执行：
- `start_fork_simulation()` / `finish_fork_simulation()`
- `execute_single_branch()` 按分支顺序串行执行（当前为模拟并行）
- `start_join_simulation()` / `finish_join_simulation()` 处理合并策略

### 5.2 重构计划：智能调度增强

在 `TopoScheduler::execute()` 中增加 `metadata.priority` 解析：

```cpp
// 计划增强（Phase 5）
// 在 Kahn 算法的 ready_queue_ 中，按 metadata.priority 排序
// 使用 std::priority_queue 替代 std::queue
struct NodePriority {
    NodePath path;
    int priority;  // 从 node.metadata["priority"] 读取，默认 0
    
    bool operator<(const NodePriority& other) const {
        return priority < other.priority; // 最大堆，高优先级先执行
    }
};
```

**性能目标：** 智能调度额外开销 < 5ms（100 节点 DAG）

---

## 6. 执行器模块

### 6.1 现有实现（`NodeExecutor`）

```cpp
class NodeExecutor {
public:
    NodeExecutor(ToolRegistry& tool_registry, LlamaAdapter* llm_adapter = nullptr);

    // 纯函数式执行：接受 Context，返回新 Context（不修改原始 Context）
    Context execute_node(Node* node, const Context& ctx);
    
    // 动态子图回调
    void set_append_graphs_callback(AppendGraphsCallback cb);
    
private:
    ToolRegistry& tool_registry_;
    LlamaAdapter* llm_adapter_;
    AppendGraphsCallback append_graphs_callback_;
    MarkdownParser markdown_parser_;  // 用于 generate_subgraph 解析

    // 权限检查
    void check_permissions(const std::vector<std::string>& perms, const NodePath& node_path);

    // 按节点类型分发
    Context execute_start(const StartNode* node, const Context& ctx);
    Context execute_end(const EndNode* node, const Context& ctx);
    Context execute_assign(const AssignNode* node, const Context& ctx);
    Context execute_llm_call(const LLMCallNode* node, const Context& ctx);
    Context execute_tool_call(const ToolCallNode* node, const Context& ctx);
    Context execute_resource(const ResourceNode* node, const Context& ctx);
    Context execute_generate_subgraph(const GenerateSubgraphNode* node, const Context& ctx);
    Context execute_join(const JoinNode* node, const Context& ctx);
    Context execute_fork(const ForkNode* node, const Context& ctx);
    Context execute_assert(const AssertNode* node, const Context& ctx);
};
```

### 6.2 重构计划：state 工具支持

在 `execute_tool_call` 中增加 `state.read` / `state.write` 路由：

```cpp
// 计划增强（Phase 5）
Context NodeExecutor::execute_tool_call(const ToolCallNode* node, const Context& ctx) {
    const auto& tool_name = node->tool_name;
    
    // state 工具路由到 StateToolRegistry
    if (tool_name == "state.read" || tool_name == "state.write") {
        return execute_state_tool(node, ctx);
    }
    
    // 普通工具调用
    check_permissions(node->permissions, node->path);
    auto result = tool_registry_.call_tool(tool_name, node->arguments);
    // ...
}
```

---

## 7. 预算控制模块

### 7.1 现有实现

**`ExecutionBudget`（`src/core/types/budget.h`）：** 使用 `std::atomic<int>` 实现线程安全的预算计数。

```cpp
struct ExecutionBudget {
    int max_nodes = -1;           // -1 表示无限制
    int max_llm_calls = -1;
    int max_duration_sec = -1;
    int max_subgraph_depth = -1;
    int max_snapshots = -1;
    size_t snapshot_max_size_kb = 512;

    // 原子计数器（线程安全）
    mutable std::atomic<int> nodes_used{0};
    mutable std::atomic<int> llm_calls_used{0};
    mutable std::atomic<int> subgraph_depth_used{0};
    std::chrono::steady_clock::time_point start_time;

    bool exceeded() const;
    bool try_consume_node();
    bool try_consume_llm_call();
    bool try_consume_subgraph_depth();
};
```

**`BudgetController`（`src/modules/budget/budget_controller.h`）：** 封装预算管理逻辑，支持预算超限跳转（默认跳转至 `/__system__/budget_exceeded`）。

### 7.2 重构计划：自适应预算

增加 `budget_inheritance: adaptive` 支持：

```cpp
// 计划新增（Phase 5）
class AdaptiveBudgetCalculator {
public:
    // 基于置信度分数（confidence_score）动态调整子图预算比例
    // ratio = 0.3 + 0.4 * confidence_score  (range: [0.3, 0.7])
    static ExecutionBudget compute_subgraph_budget(
        const ExecutionBudget& parent_budget,
        float confidence_score
    );
    
    // 性能要求：< 1ms
    static float estimate_confidence(const Context& ctx);
};
```

---

## 8. 上下文管理模块

### 8.1 现有实现（`ContextEngine`）

```cpp
// Context = nlohmann::json（别名，定义于 src/core/types/context.h）
using Value = nlohmann::json;
using Context = nlohmann::json;

class ContextEngine {
public:
    // 执行节点并处理快照
    Result execute_with_snapshot(
        std::function<Context(const Context&)> execute_func,
        const Context& ctx,
        bool need_snapshot,
        const NodePath& snapshot_node_path
    );

    // 静态合并（Fork/Join 分支结果合并）
    static void merge(Context& target, const Context& source, 
                      const ContextMergePolicy& policy = {});

    // 快照管理
    void save_snapshot(const NodePath& key, const Context& ctx);
    const Context* get_snapshot(const NodePath& key) const;
    void enforce_snapshot_budget();
    void set_snapshot_limits(size_t max_count, size_t max_size_kb);
};

// 合并策略（字符串枚举）
// "error_on_conflict" | "last_write_wins" | "deep_merge" | "array_concat" | "array_merge_unique"
using MergeStrategy = std::string;
```

**Fork/Join 合并策略：** `JoinNode::merge_strategy` 字段控制分支结果合并行为，当前支持 `error_on_conflict`（默认）、`last_write_wins`、`deep_merge`、`array_concat`、`array_merge_unique`。

---

## 9. 工具注册系统

### 9.1 现有实现（`ToolRegistry`）

```cpp
class ToolRegistry {
public:
    ToolRegistry();  // 构造时注册默认工具

    template<typename Func>
    void register_tool(std::string name, Func&& func);

    bool has_tool(const std::string& name) const;
    
    // 调用工具，传入 key-value 参数，返回 JSON 结果
    nlohmann::json call_tool(const std::string& name, 
                              const std::unordered_map<std::string, std::string>& args);
    
    std::vector<std::string> list_tools() const;
};
```

`DSLEngine` 通过 `register_tool<Func>()` 模板方法允许宿主程序注册自定义工具：

```cpp
engine->register_tool("my_tool", [](const auto& args) -> nlohmann::json {
    return { {"result", args.at("input")} };
});
```

### 9.2 重构计划：state 工具注册（v2.2）

```cpp
// 计划新增（Phase 5）
// 在 L2（WorkflowEngine）中注册 state 工具到 ToolRegistry
engine->register_tool("state.read", [&state_manager](const auto& args) -> nlohmann::json {
    return state_manager.read(args.at("key"));
});

engine->register_tool("state.write", [&state_manager](const auto& args) -> nlohmann::json {
    state_manager.write(args.at("key"), args.at("value"));
    return { {"success", true} };
});
```

---

## 10. LLM 适配器

### 10.1 现有实现（`LlamaAdapter`）

当前 L0 仅支持本地 llama.cpp 后端：

```cpp
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
    std::string generate(const std::string& prompt);
    bool is_loaded() const;
};

// 注意：当前存在全局指针（计划重构为依赖注入）
extern LlamaAdapter* g_current_llm_adapter;
```

### 10.2 重构计划：多后端适配器接口

将 `LlamaAdapter` 重构为实现 `ILLMProvider` 接口，支持多后端：

```cpp
// 计划新增（Phase 2）
class ILLMProvider {
public:
    virtual ~ILLMProvider() = default;
    virtual std::string generate(const std::string& prompt) = 0;
    virtual bool is_loaded() const = 0;
};

class LlamaAdapter : public ILLMProvider { /* 现有实现 */ };
class OpenAIAdapter : public ILLMProvider { /* 新增：OpenAI HTTP 适配 */ };
class AnthropicAdapter : public ILLMProvider { /* 新增：Anthropic HTTP 适配 */ };

// 同时移除全局指针 g_current_llm_adapter，改为依赖注入
```

---

## 11. 标准库层（对应 AgenticOS L2.5）

### 11.1 现有实现

```cpp
class StandardLibraryLoader {
public:
    static StandardLibraryLoader& instance();  // 单例
    const std::vector<LibraryEntry>& get_available_libraries() const;
    void load_from_directory(const std::string& lib_dir);
    void load_builtin_libraries();
};
```

当前 `lib/` 目录结构：
```text
lib/
├── auth/       # 认证相关工具
├── human/      # 人机交互工具
├── math/       # 数学计算工具
└── utils/      # 通用工具
```

### 11.2 重构计划：分层标准库（v2.2）

按 AgenticOS v2.2 架构，将标准库重组为三层：

```text
lib/
├── cognitive/   # L4 认知层专用（Cognitive Profile，仅允许 state.read/write）
├── thinking/    # L3 推理层专用（Thinking Profile，禁止 state.write）
├── workflow/    # L2 工作流专用（Workflow Profile，无额外约束）
└── utils/       # 通用工具（跨层使用）
```

---

## 12. 追踪导出模块

### 12.1 现有实现（`TraceExporter`）

```cpp
struct TraceRecord {
    std::string trace_id;
    NodePath node_path;
    std::string type;       // NodeType 字符串
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    std::string status;     // "success" | "failed" | "skipped"
    std::optional<std::string> error_code;
    nlohmann::json context_delta;      // 执行前后上下文差量
    std::optional<NodePath> ctx_snapshot_key;
    nlohmann::json budget_snapshot;   // 执行时预算状态
    nlohmann::json metadata;          // 节点原始 metadata
    std::optional<std::string> llm_intent;
    std::string mode;       // "dev" | "prod"
};

class TraceExporter {
public:
    void on_node_start(const NodePath& path, NodeType type,
                       const nlohmann::json& initial_context,
                       const std::optional<ExecutionBudget>& budget);
    
    void on_node_end(const NodePath& path, const std::string& status,
                     const std::optional<std::string>& error_code,
                     const nlohmann::json& initial_context,
                     const nlohmann::json& final_context,
                     const std::optional<NodePath>& snapshot_key,
                     const std::optional<ExecutionBudget>& budget);
    
    std::vector<TraceRecord> get_traces() const;
    void clear_traces();
};
```

---

## 13. Python 绑定计划（pybind11）

### 13.1 重构目标

当前代码库没有 Python 绑定。重构后，通过 pybind11 暴露最小化接口（Thin Wrapper）：

```cpp
// 计划新增：src/bindings/python.cpp
#include <pybind11/pybind11.h>
#include "core/engine.h"

namespace py = pybind11;

PYBIND11_MODULE(agentic_dsl_runtime, m) {
    py::class_<DSLEngine>(m, "DSLEngine")
        .def_static("from_markdown", &DSLEngine::from_markdown)
        .def_static("from_file", &DSLEngine::from_file)
        .def("run", &DSLEngine::run,
             py::arg("context") = Context{})
        .def("register_tool", [](DSLEngine& self, const std::string& name, py::function fn) {
            self.register_tool(name, [fn](const std::unordered_map<std::string, std::string>& args) 
                               -> nlohmann::json {
                // 转换 args 到 Python dict 并调用
                return py::cast<nlohmann::json>(fn(args));
            });
        })
        .def("get_last_traces", &DSLEngine::get_last_traces);
}
```

**约束：** Python 层不包含任何业务逻辑，所有逻辑通过 DSL 子图实现。

---

## 14. 重构实施计划

### Phase 1：代码整理与接口规范化（基础重构）

| 任务 | 现有状态 | 目标 | 优先级 |
| :--- | :--- | :--- | :--- |
| 移除 `g_current_llm_adapter` 全局指针 | `llama_adapter.h:41` 存在 | 改为依赖注入 | P0 |
| 规范化命名空间 `agenticdsl` | 部分文件已使用 | 统一所有模块 | P0 |
| 提取 `ILLMProvider` 接口 | 无 | 抽象 LLM 适配器 | P1 |
| `StandardLibraryLoader` 去单例化 | 当前为 `instance()` 单例 | 改为依赖注入 | P1 |
| 增加 `DSLEngine::compile()` 纯函数接口 | 当前为 `from_markdown()` + `run()` | 分离编译与执行 | P1 |

### Phase 2：多 LLM 后端支持

| 任务 | 说明 | 依赖 |
| :--- | :--- | :--- |
| 提取 `ILLMProvider` 接口 | 参见第 10.2 节 | Phase 1 完成 |
| 实现 `OpenAIAdapter` | HTTP/JSON-RPC 适配 | `ILLMProvider` |
| 实现 `AnthropicAdapter` | Anthropic Messages API | `ILLMProvider` |
| 实现 `LLMProviderFactory` | 根据配置选择后端 | 所有 Adapter |

### Phase 3：标准库分层重组

| 任务 | 说明 | 依赖 |
| :--- | :--- | :--- |
| 创建 `/lib/cognitive/` 目录 | 迁移 L4 认知层子图 | Phase 1 完成 |
| 创建 `/lib/thinking/` 目录 | 迁移 L3 推理层子图 | Phase 1 完成 |
| 创建 `/lib/workflow/` 目录 | 迁移现有 `/lib/` 工具 | Phase 1 完成 |
| 增强 `StandardLibraryLoader` | 按层加载，签名验证 | 分层目录完成 |

### Phase 4：pybind11 Python 绑定

| 任务 | 说明 | 依赖 |
| :--- | :--- | :--- |
| 集成 pybind11 | `CMakeLists.txt` 增加 pybind11 | Phase 1 完成 |
| 实现 `src/bindings/python.cpp` | 参见第 13.1 节 | Phase 1 完成 |
| 创建 `pyproject.toml` | Python 包配置 | 绑定完成 |
| 测试 `test_bindings.py` | Python Thin Wrapper 验证 | 绑定完成 |

### Phase 5：智能化演进特性（v2.2 核心）

| 任务 | 说明 | 依赖 |
| :--- | :--- | :--- |
| 增加 `SemanticValidator` | Layer Profile 编译时验证 | Phase 1 完成 |
| 智能调度（metadata.priority） | `TopoScheduler` 优先级队列 | Phase 1 完成 |
| 自适应预算（`AdaptiveBudgetCalculator`） | 置信度驱动预算分配 | Phase 1 完成 |
| `state.read`/`state.write` 工具路由 | `NodeExecutor` 增强 | `ILLMProvider`、Phase 3 |
| 风险感知人机协作 | `require_human_approval: risk_based` | Phase 5 前期 |

---

## 15. 目标模块结构

重构后的 `agentic-dsl-runtime` 目录结构：

```text
agentic-dsl-runtime/
├── src/
│   ├── core/
│   │   ├── engine.h / engine.cpp          # DSLEngine（主入口，新增 compile() 接口）
│   │   └── types/
│   │       ├── node.h                     # 节点类型（新增 STATE_READ/STATE_WRITE）
│   │       ├── budget.h                   # ExecutionBudget（新增 adaptive 模式）
│   │       ├── context.h                  # Context（nlohmann::json）
│   │       └── resource.h
│   ├── common/
│   │   ├── llm/
│   │   │   ├── illm_provider.h            # 新增：ILLMProvider 接口
│   │   │   ├── llama_adapter.h/cpp        # 重构：实现 ILLMProvider
│   │   │   ├── openai_adapter.h/cpp       # 新增：OpenAI HTTP 适配
│   │   │   ├── anthropic_adapter.h/cpp    # 新增：Anthropic 适配
│   │   │   └── llm_provider_factory.h/cpp # 新增：工厂方法
│   │   └── tools/
│   │       ├── registry.h/cpp             # 保留
│   │       └── state_tool_registry.h/cpp  # 新增：state 工具注册与验证
│   └── modules/
│       ├── parser/
│       │   ├── markdown_parser.h/cpp      # 保留
│       │   └── semantic_validator.h/cpp   # 新增：语义分析（Layer Profile 验证）
│       ├── scheduler/
│       │   ├── topo_scheduler.h/cpp       # 增强：优先级队列
│       │   ├── execution_session.h/cpp    # 保留
│       │   └── resource_manager.h/cpp     # 保留
│       ├── executor/
│       │   ├── node_executor.h/cpp        # 增强：state 工具路由
│       │   └── node.cpp
│       ├── budget/
│       │   ├── budget_controller.h/cpp    # 保留
│       │   └── adaptive_budget.h/cpp      # 新增：自适应预算计算
│       ├── context/
│       │   └── context_engine.h/cpp       # 保留
│       ├── trace/
│       │   └── trace_exporter.h/cpp       # 保留
│       ├── library/
│       │   ├── library_loader.h/cpp       # 增强：分层加载
│       │   └── schema.h
│       ├── system/
│       │   └── system_nodes.h/cpp         # 保留
│       └── bindings/
│           └── python.cpp                 # 新增：pybind11 Thin Wrapper
├── lib/
│   ├── cognitive/                         # 新增：L4 认知层标准库
│   ├── thinking/                          # 新增：L3 推理层标准库
│   ├── workflow/                          # 新增：工作流标准库（迁移现有 lib/）
│   └── utils/                             # 保留：通用工具
├── include/
│   └── agentic_dsl/                       # 新增：公共头文件（对外暴露）
│       ├── engine.h
│       ├── types.h
│       └── llm_provider.h
├── tests/
│   ├── test_parser.cpp                    # 保留（现有）
│   ├── test_scheduler.cpp                 # 保留（现有）
│   ├── test_engine.cpp                    # 保留（现有）
│   ├── test_no_llm.cpp                    # 保留（现有）
│   ├── test_basic.cpp                     # 保留（现有）
│   ├── test_library_loader.cpp            # 保留（现有）
│   ├── test_layer_profile.cpp             # 新增：Layer Profile 验证
│   ├── test_state_tool.cpp                # 新增：state 工具注册
│   ├── test_adaptive_budget.cpp           # 新增：自适应预算
│   └── test_bindings.py                   # 新增：Python 绑定
├── CMakeLists.txt                         # 增加 pybind11 支持
├── pyproject.toml                         # 新增：Python 包配置
└── README.md
```

---

## 16. 接口契约

### 16.1 L0/L2 边界（核心约束）

```
L2（WorkflowEngine）             L0（agentic-dsl-runtime）
         │                               │
         │  compile(markdown_source)     │
         │──────────────────────────────▶│
         │  ◀────── ParsedGraph[] ───────│
         │                               │
         │  execute(graphs, ctx, budget) │
         │──────────────────────────────▶│
         │  ◀──── ExecutionResult ───────│
         │                               │
         │  register_tool("state.read")  │
         │──────────────────────────────▶│
         │                               │
```

**约束：**
- L2 不直接访问 `NodeExecutor`、`TopoScheduler` 内部状态
- L2 通过 `DSLEngine::register_tool()` 注入 `state.read`/`state.write` 实现
- L0 不维护跨调用的持久状态（每次 `run()` 是独立执行）

### 16.2 Python Thin Wrapper 接口

```python
import agentic_dsl_runtime as runtime

# 编译 DSL
engine = runtime.DSLEngine.from_file("workflow.md")

# 注册工具（从 L2 传入）
engine.register_tool("state.read", lambda args: state_manager.read(args["key"]))
engine.register_tool("state.write", lambda args: state_manager.write(args["key"], args["value"]))

# 执行
result = engine.run({"input": "hello"})

# 获取追踪
traces = engine.get_last_traces()
```

---

## 17. 性能目标

| 指标 | 目标值 | 备注 |
| :--- | :--- | :--- |
| DSL 解析（1000 行） | < 50ms | `MarkdownParser` |
| 拓扑排序（100 节点） | < 5ms | `TopoScheduler::build_dag()` |
| 智能调度额外开销 | < 5ms | 100 节点 DAG |
| 节点执行（assign/condition） | < 1ms | 纯 CPU |
| 自适应预算计算 | < 1ms | `AdaptiveBudgetCalculator` |
| LLM 调用延迟 | 不含 LLM 响应时间 | 适配器开销 < 10ms |

---

## 18. 风险与缓解

| 风险 | 影响 | 缓解措施 |
| :--- | :--- | :--- |
| `g_current_llm_adapter` 全局状态 | 多实例冲突 | Phase 1 优先移除，改为依赖注入 |
| `StandardLibraryLoader` 单例 | 测试隔离困难 | Phase 1 去单例化 |
| Fork/Join 当前为串行模拟 | 无真正并行 | 评估线程池方案（Phase 3） |
| Markdown 解析格式变更 | 与其他工具兼容性 | 保持向后兼容，增加格式版本标记 |
| pybind11 GIL 开销 | Python 调用性能 | 通过 `py::gil_scoped_release` 释放 GIL |
| Layer Profile 验证遗漏 | 权限绕过 | 编译期 + 运行期双重验证 |
| ABI 兼容性 | 第三方集成失败 | 符号版本控制，`include/agentic_dsl/` 稳定接口 |

---

## 19. 与 AgenticOS 架构层对应关系

| AgenticOS 层 | 对应组件 | 实现位置 |
| :--- | :--- | :--- |
| **L0** | `DSLEngine`, `MarkdownParser`, `TopoScheduler`, `NodeExecutor` | `src/core/`, `src/modules/` |
| **L0** | `BudgetController`, `ContextEngine`, `TraceExporter` | `src/modules/budget/`, `context/`, `trace/` |
| **L0** | `LlamaAdapter`（重构为 `ILLMProvider`） | `src/common/llm/` |
| **L0** | `ToolRegistry`（含 state 工具） | `src/common/tools/` |
| **L2.5** | `/lib/cognitive/`, `/lib/thinking/`, `/lib/workflow/` | `lib/` |
| **L2** | `WorkflowEngine`（注册 state 工具，调用 `DSLEngine`） | 独立项目（AgenticOS L2） |
| **L4** | `CognitiveStateManager`（提供 `state.read`/`state.write` 实现） | 独立项目（AgenticOS L4） |

---

## 20. 文档清单

| 文档 | 路径 | 状态 |
| :--- | :--- | :--- |
| AgenticOS 架构总纲 | `docs/AgenticOS_Architecture.md` | 已发布 |
| **Layer 0 重构规范**（本文档） | `docs/AgenticOS_Layer0_Spec.md` | **当前** |
| DSL 标准库规范 | `docs/AgenticDSL_LibSpec_v3.9.md` | 已发布 |
| DSL 语言规范 | `docs/AgenticDSL_v3.9.md` | 已发布 |
| 运行时开发指南 | `docs/AgenticDSL_RTGuide.md` | 已发布 |
| 应用开发指南（C++） | `docs/AgenticDSL_AppDevGuide_C++_part.md` | 已发布 |

---

**文档结束**  
**基于代码库：** `chisuhua/AgenticDSL`（`src/` 目录，2026-02-25）  
**下一步：** 按 Phase 1 开始代码重构，移除全局状态，规范化接口
