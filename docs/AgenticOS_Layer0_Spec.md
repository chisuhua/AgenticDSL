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

## 14. 重构实施计划（详细）

### 总体原则

1. **最小化破坏性变更**：优先扩展接口而不是替换，保持现有测试通过
2. **增量迁移**：每个 Phase 独立可测试、可回滚
3. **现有测试先行**：每个 Phase 开始前确认 `AGENTICDSL_BUILD_TESTS=ON` 下所有现有测试通过

### Phase 总览

| Phase | 名称 | 关键任务 | 新增文件 | 预估工作量 |
| :--- | :--- | :--- | :--- | :--- |
| **Phase 1** | 代码整理与接口规范化 | 移除全局指针、去单例化、compile() 接口、结构化错误 | `src/core/types/errors.h` | 1-2 周 |
| **Phase 2** | 多 LLM 后端支持 | ILLMProvider 接口、OpenAI/Anthropic 适配器、工厂 | `illm_provider.h`, `openai_adapter.h/cpp`, `anthropic_adapter.h/cpp`, `llm_provider_factory.h/cpp` | 2-3 周 |
| **Phase 3** | 标准库分层重组 | lib/ 目录重组、StandardLibraryLoader 增强 | `lib/cognitive/`, `lib/thinking/`, `lib/workflow/` | 1-2 周 |
| **Phase 4** | Python 绑定 | pybind11 集成、Thin Wrapper 实现、pyproject.toml | `src/modules/bindings/python.cpp`, `pyproject.toml` | 1-2 周 |
| **Phase 5** | 智能化演进（v2.2） | SemanticValidator、智能调度、自适应预算、state 工具、人机协作 | `semantic_validator.h/cpp`, `adaptive_budget.h/cpp` | 3-4 周 |

---

### Phase 1：代码整理与接口规范化

**目标：** 消除全局状态，统一接口，为后续 Phase 奠定基础。

#### 1.1 移除 `g_current_llm_adapter` 全局指针（P0）

**问题根源（`src/core/engine.cpp`，第 140-143 行）：**

```cpp
// 当前问题代码
LlamaAdapter* prev = g_current_llm_adapter;
g_current_llm_adapter = llama_adapter_.get();  // ← 全局状态写入
auto result = scheduler.execute(context);
g_current_llm_adapter = prev;                   // ← 恢复（不支持并发）
```

**重构方案：** 将 `llm_adapter` 通过 `TopoScheduler::Config` 传入，让调度器直接持有指针（目前已通过构造函数参数传递，但引擎层仍设置全局变量）。

**修改 `src/common/llm/llama_adapter.h`：**

```cpp
// 删除以下行
extern LlamaAdapter* g_current_llm_adapter;
```

**修改 `src/common/llm/llama_adapter.cpp`：**

```cpp
// 删除以下行
LlamaAdapter* g_current_llm_adapter = nullptr;
```

**修改 `src/core/engine.cpp` 的 `run()` 方法：**

```cpp
// 重构后：移除全局指针操作
ExecutionResult DSLEngine::run(const Context& context) {
    std::optional<ExecutionBudget> budget;
    for (auto& g : full_graphs_) {
        if (g.budget.has_value()) {
            budget = std::move(g.budget);
            break;
        }
    }

    TopoScheduler::Config config;
    config.initial_budget = std::move(budget);
    // llama_adapter_ 已通过构造函数参数传入 TopoScheduler，无需全局指针
    TopoScheduler scheduler(std::move(config), tool_registry_,
                            llama_adapter_.get(), &full_graphs_);

    auto sys_nodes = create_system_nodes();
    for (auto& node : sys_nodes) scheduler.register_node(std::move(node));
    for (const auto& graph : full_graphs_)
        for (const auto& node : graph.nodes)
            if (node) scheduler.register_node(node->clone());
    scheduler.build_dag();

    // 移除：g_current_llm_adapter 的设置/恢复
    auto result = scheduler.execute(context);
    last_traces_ = scheduler.get_last_traces();
    return result;
}
```

**影响范围检查：** 搜索代码库中所有引用 `g_current_llm_adapter` 的位置，确认仅存在于 `llama_adapter.h/cpp` 和 `engine.cpp`，移除后不影响其他代码。

---

#### 1.2 `StandardLibraryLoader` 去单例化（P1）

**问题根源（`src/modules/library/library_loader.cpp`，第 10-18 行）：**

```cpp
// 当前：全局单例，难以在测试中隔离
StandardLibraryLoader& StandardLibraryLoader::instance() {
    static StandardLibraryLoader loader;
    static bool initialized = false;
    if (!initialized) {
        loader.load_builtin_libraries();
        initialized = true;
    }
    return loader;
}
```

**重构方案：** 保留 `instance()` 作为便利方法（向后兼容），但同时支持非单例构造，在测试和 L0 重构中使用非单例实例。

**修改 `src/modules/library/library_loader.h`：**

```cpp
class StandardLibraryLoader {
public:
    // 新增：非单例构造函数（用于测试和依赖注入）
    explicit StandardLibraryLoader(bool load_builtins = true);

    // 保留：便利单例（向后兼容）
    static StandardLibraryLoader& instance();

    const std::vector<LibraryEntry>& get_available_libraries() const;
    void load_from_directory(const std::string& lib_dir);
    void load_builtin_libraries();

private:
    std::vector<LibraryEntry> libraries_;
    MarkdownParser parser_;
};
```

**修改 `src/modules/library/library_loader.cpp`：**

```cpp
// 新增：带参数的构造函数
StandardLibraryLoader::StandardLibraryLoader(bool load_builtins) {
    if (load_builtins) {
        load_builtin_libraries();
    }
}

// 保留：单例（现在调用带参构造）
StandardLibraryLoader& StandardLibraryLoader::instance() {
    static StandardLibraryLoader loader(true); // load_builtins = true
    return loader;
}
```

**测试验证：** 在现有测试 `test_library_loader.cpp` 中增加：

```cpp
TEST_CASE("StandardLibraryLoader non-singleton", "[library]") {
    // 非单例实例，空库（不加载内置）
    StandardLibraryLoader loader(false);
    REQUIRE(loader.get_available_libraries().empty());

    // 手动加载内置
    loader.load_builtin_libraries();
    REQUIRE_FALSE(loader.get_available_libraries().empty());
}
```

---

#### 1.3 增加 `DSLEngine::compile()` 纯函数接口（P1）

**目标：** 将 DSL 解析（`from_markdown`）与执行（`run`）在接口层面更清晰分离，供 L2 单独调用解析阶段。

**修改 `src/core/engine.h`：**

```cpp
class DSLEngine {
public:
    // 现有接口（保留）
    static std::unique_ptr<DSLEngine> from_markdown(const std::string& markdown_content);
    static std::unique_ptr<DSLEngine> from_file(const std::string& file_path);
    ExecutionResult run(const Context& context = Context{});

    // 新增：仅编译，不执行（纯函数，供 L2 独立调用）
    static std::vector<ParsedGraph> compile(const std::string& markdown_content);

    // 新增：从已编译的 ParsedGraph 创建引擎（L2 用）
    static std::unique_ptr<DSLEngine> from_graphs(std::vector<ParsedGraph> graphs);

    // 保留其他现有方法...
};
```

**修改 `src/core/engine.cpp`：**

```cpp
// 新增：compile() - 纯解析，不加载 LLM
std::vector<ParsedGraph> DSLEngine::compile(const std::string& markdown_content) {
    MarkdownParser parser;
    auto graphs = parser.parse_from_string(markdown_content);
    bool has_main = false;
    for (const auto& g : graphs) {
        if (g.path == "/main") { has_main = true; break; }
    }
    if (!has_main) {
        throw std::runtime_error("Required /main subgraph not found");
    }
    return graphs;
}

// 新增：from_graphs() - 从已解析图创建引擎
std::unique_ptr<DSLEngine> DSLEngine::from_graphs(std::vector<ParsedGraph> graphs) {
    auto config = load_llm_config();
    auto engine = std::make_unique<DSLEngine>(std::move(graphs));
    engine->llama_adapter_ = std::make_unique<LlamaAdapter>(config);
    return engine;
}
```

---

#### 1.4 统一错误处理机制（P1）

**问题：** 当前错误通过 `throw std::runtime_error` 抛出，错误信息字符串不一致（如 `execute_assert` 将跳转目标编码在消息字符串中，由调度器解析）。

**重构方案：** 引入结构化错误类型：

```cpp
// 新增：src/core/types/errors.h
namespace agenticdsl {

enum class ErrorCode {
    ASSERT_FAILED,
    BUDGET_EXCEEDED,
    TOOL_NOT_FOUND,
    TOOL_PERMISSION_DENIED,
    LLM_NOT_AVAILABLE,
    NODE_NOT_FOUND,
    CYCLE_DETECTED,
    SIGNATURE_VALIDATION_FAILED,
    LAYER_PROFILE_VIOLATION,    // v2.2 新增
    STATE_TOOL_NOT_REGISTERED,  // v2.2 新增
};

struct DSLError : public std::exception {
    ErrorCode code;
    std::string message;
    std::optional<NodePath> jump_target; // 用于 assert 跳转

    DSLError(ErrorCode c, std::string msg, std::optional<NodePath> jump = std::nullopt)
        : code(c), message(std::move(msg)), jump_target(std::move(jump)) {}

    const char* what() const noexcept override { return message.c_str(); }
};

} // namespace agenticdsl
```

**修改 `execute_assert` 中的错误抛出（`src/modules/executor/node_executor.cpp`）：**

```cpp
// 重构前（编码跳转路径为字符串）
throw std::runtime_error("Assert failed. Jumping to: " + node->on_failure.value());

// 重构后（结构化错误）
throw DSLError(ErrorCode::ASSERT_FAILED,
               "Assert failed at node: " + node->path,
               node->on_failure);
```

**修改 `topo_scheduler.cpp` 中的错误处理：**

```cpp
// 重构前（解析字符串）
if (session_result.message.find("Jumping to:") != std::string::npos) {
    size_t pos = session_result.message.find("Jumping to:");
    NodePath target = session_result.message.substr(pos + 12);
    // ...
}

// 重构后（捕获结构化错误）
// 在 ExecutionSession::execute_node 中捕获 DSLError，
// 将 jump_target 返回到 ExecutionResult
struct ExecutionResult {
    Context new_context;
    bool success;
    std::string message;
    std::optional<NodePath> snapshot_key;
    std::optional<NodePath> paused_at;
    std::optional<NodePath> jump_target; // 新增
};
```

---

### Phase 2：多 LLM 后端支持

**目标：** 提取 `ILLMProvider` 接口，支持 OpenAI、Anthropic、本地 llama.cpp 多后端，通过 `llm_config.json` 配置切换。

#### 2.1 `ILLMProvider` 接口定义

**新增 `src/common/llm/illm_provider.h`：**

```cpp
#pragma once
#include <string>
#include <memory>

namespace agenticdsl {

class ILLMProvider {
public:
    virtual ~ILLMProvider() = default;

    // 生成文本（阻塞调用）
    virtual std::string generate(const std::string& prompt) = 0;

    // 检查是否已加载/可用
    virtual bool is_loaded() const = 0;

    // 后端名称（用于日志和配置）
    virtual std::string backend_name() const = 0;
};

} // namespace agenticdsl
```

#### 2.2 重构 `LlamaAdapter`

**修改 `src/common/llm/llama_adapter.h`：**

```cpp
#include "illm_provider.h"

namespace agenticdsl {

class LlamaAdapter : public ILLMProvider {  // 新增继承
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

    std::string generate(const std::string& prompt) override;
    bool is_loaded() const override;
    std::string backend_name() const override { return "llama.cpp"; }

    // ... 私有成员不变 ...
};

// 删除全局指针声明：
// extern LlamaAdapter* g_current_llm_adapter;  ← 删除

} // namespace agenticdsl
```

#### 2.3 新增 `OpenAIAdapter`

**新增 `src/common/llm/openai_adapter.h`：**

```cpp
#pragma once
#include "illm_provider.h"
#include <string>

namespace agenticdsl {

class OpenAIAdapter : public ILLMProvider {
public:
    struct Config {
        std::string api_key;
        std::string base_url = "https://api.openai.com/v1";
        std::string model = "gpt-4o";
        float temperature = 0.7f;
        int max_tokens = 2048;
        int timeout_sec = 30;
    };

    explicit OpenAIAdapter(const Config& config);

    std::string generate(const std::string& prompt) override;
    bool is_loaded() const override { return !config_.api_key.empty(); }
    std::string backend_name() const override { return "openai"; }

private:
    Config config_;
    // HTTP 调用实现（可选：libcurl 或内置 HTTP 客户端）
    std::string http_post(const std::string& url, const std::string& body);
};

} // namespace agenticdsl
```

**`OpenAIAdapter::generate` 实现（`src/common/llm/openai_adapter.cpp`）：**

```cpp
std::string OpenAIAdapter::generate(const std::string& prompt) {
    // 构建 OpenAI Chat Completion 请求体
    nlohmann::json request = {
        {"model", config_.model},
        {"messages", {{{"role", "user"}, {"content", prompt}}}},
        {"temperature", config_.temperature},
        {"max_tokens", config_.max_tokens}
    };

    std::string response_body = http_post(
        config_.base_url + "/chat/completions",
        request.dump()
    );

    // 解析响应
    auto resp_json = nlohmann::json::parse(response_body);
    return resp_json["choices"][0]["message"]["content"].get<std::string>();
}
```

#### 2.4 `LLMProviderFactory`

**新增 `src/common/llm/llm_provider_factory.h`：**

```cpp
#pragma once
#include "illm_provider.h"
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

namespace agenticdsl {

class LLMProviderFactory {
public:
    // 从 llm_config.json 创建适配器
    // 配置格式：{ "backend": "llama|openai|anthropic", ... }
    static std::unique_ptr<ILLMProvider> create_from_config(
        const std::string& config_path = "llm_config.json");

    // 直接从 JSON 对象创建
    static std::unique_ptr<ILLMProvider> create_from_json(const nlohmann::json& config);
};

} // namespace agenticdsl
```

**`llm_config.json` 配置示例（扩展当前格式）：**

```json
{
  "backend": "openai",
  "openai": {
    "api_key": "${OPENAI_API_KEY}",
    "model": "gpt-4o",
    "temperature": 0.7,
    "max_tokens": 2048
  },
  "llama": {
    "model_path": "models/qwen-0.6b.gguf",
    "n_ctx": 2048,
    "n_threads": 4
  }
}
```

#### 2.5 修改 `DSLEngine` 使用 `ILLMProvider`

**修改 `src/core/engine.h`：**

```cpp
#include "common/llm/illm_provider.h"  // 替换 llama_adapter.h

class DSLEngine {
    // ...
private:
    std::unique_ptr<ILLMProvider> llm_provider_;  // 替换 llama_adapter_
    // ...
};
```

**修改 `src/core/engine.cpp`：**

```cpp
std::unique_ptr<DSLEngine> DSLEngine::from_markdown(const std::string& markdown_content) {
    MarkdownParser parser;
    auto graphs = parser.parse_from_string(markdown_content);
    // ...
    auto engine = std::make_unique<DSLEngine>(std::move(graphs));
    engine->llm_provider_ = LLMProviderFactory::create_from_config(); // 工厂创建
    return engine;
}
```

**测试验证（新增 `tests/test_llm_provider.cpp`）：**

```cpp
TEST_CASE("LLMProviderFactory selects llama backend", "[llm]") {
    nlohmann::json config = {{"backend", "llama"}, {"llama", {{"model_path", "none"}}}};
    // 仅测试工厂选择逻辑，不实际加载模型
    // 验证返回 LlamaAdapter 实例
}

TEST_CASE("OpenAIAdapter config parsing", "[llm]") {
    nlohmann::json config = {
        {"backend", "openai"},
        {"openai", {{"api_key", "test-key"}, {"model", "gpt-4o"}}}
    };
    auto provider = LLMProviderFactory::create_from_json(config);
    REQUIRE(provider->backend_name() == "openai");
    REQUIRE(provider->is_loaded() == true); // api_key is set
}
```

---

### Phase 3：标准库分层重组

**目标：** 将 `lib/` 目录按 AgenticOS 层级重组，增强 `StandardLibraryLoader` 支持层级感知加载和签名验证。

#### 3.1 目录迁移计划

**现有 `lib/` 结构：**
```text
lib/
├── auth/       → 迁移到 lib/workflow/auth/
├── human/      → 迁移到 lib/workflow/human/
├── math/       → 迁移到 lib/workflow/math/（工具函数，Workflow Profile）
└── utils/      → 迁移到 lib/utils/（跨层通用）
```

**目标结构：**
```text
lib/
├── cognitive/                  # L4 认知层（Cognitive Profile）
│   ├── route_decision.md       # 路由决策子图
│   └── confidence_eval.md      # 置信度评估子图
├── thinking/                   # L3 推理层（Thinking Profile）
│   ├── react_loop.md           # ReAct 循环子图
│   └── with_rollback.md        # 现有 /lib/reasoning/with_rollback
├── workflow/                   # L2 工作流层（Workflow Profile）
│   ├── auth/                   # 认证工具（从 lib/auth/ 迁移）
│   ├── human/                  # 人机交互（从 lib/human/ 迁移）
│   └── math/                   # 数学计算（从 lib/math/ 迁移）
└── utils/                      # 通用工具（跨层）
    └── noop.md
```

#### 3.2 DSL 文件中的 Layer Profile 声明

每个标准库 DSL 文件需在 `/__meta__` 块中声明 `layer_profile`：

**示例：`lib/cognitive/route_decision.md`**
```markdown
### AgenticDSL `/__meta__`
```yaml
# --- BEGIN AgenticDSL ---
layer_profile: Cognitive
version: "1.0"
# --- END AgenticDSL ---
```

### AgenticDSL `/lib/cognitive/route_decision`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
signature: "(context: object, options: array) -> {chosen_path: string}"
permissions: ["state:read"]
nodes:
  - id: start
    type: start
    next: ["/lib/cognitive/route_decision/decide"]
  - id: decide
    type: llm_call
    prompt_template: "Given context {{ context }}, choose from {{ options }}..."
    output_keys: ["chosen_path"]
    next: ["/lib/cognitive/route_decision/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
```

#### 3.3 增强 `StandardLibraryLoader`

**修改 `src/modules/library/library_loader.h`：**

```cpp
// 新增字段
struct LibraryEntry {
    NodePath path;
    std::optional<std::string> signature;
    std::optional<nlohmann::json> output_schema;
    std::vector<std::string> permissions;
    bool is_subgraph = true;
    std::string layer_profile = "Workflow"; // 新增：Cognitive/Thinking/Workflow
};

class StandardLibraryLoader {
public:
    explicit StandardLibraryLoader(bool load_builtins = true);
    static StandardLibraryLoader& instance();

    // 新增：按层加载
    void load_layer(const std::string& lib_dir, const std::string& layer_profile);

    // 新增：按 Profile 查询
    std::vector<LibraryEntry> get_libraries_for_profile(
        const std::string& profile) const;

    // 新增：签名验证
    bool validate_signature(const NodePath& lib_path,
                            const nlohmann::json& input) const;

    const std::vector<LibraryEntry>& get_available_libraries() const;
    void load_from_directory(const std::string& lib_dir);
    void load_builtin_libraries();
};
```

**修改 `src/modules/library/library_loader.cpp`：**

```cpp
void StandardLibraryLoader::load_layer(const std::string& lib_dir,
                                        const std::string& layer_profile) {
    namespace fs = std::filesystem;
    if (!fs::exists(lib_dir) || !fs::is_directory(lib_dir)) return;

    for (const auto& entry : fs::recursive_directory_iterator(lib_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".md") continue;

        std::ifstream file(entry.path());
        std::stringstream buffer;
        buffer << file.rdbuf();
        try {
            auto graphs = parser_.parse_from_string(buffer.str());
            for (const auto& g : graphs) {
                if (g.is_standard_library) {
                    LibraryEntry e;
                    e.path = g.path;
                    e.signature = g.signature;
                    e.output_schema = g.output_schema;
                    e.permissions = g.permissions;
                    e.is_subgraph = true;
                    e.layer_profile = layer_profile; // 标记层级
                    libraries_.push_back(std::move(e));
                }
            }
        } catch (...) { /* 跳过加载失败的文件 */ }
    }
}

void StandardLibraryLoader::load_builtin_libraries() {
    // 按层加载（Phase 3 后使用分层目录）
    load_layer("lib/cognitive", "Cognitive");
    load_layer("lib/thinking", "Thinking");
    load_layer("lib/workflow", "Workflow");
    load_layer("lib/utils", "Workflow"); // 通用工具默认 Workflow Profile

    // 保留内置硬编码条目（向后兼容）
    libraries_.push_back({
        "/lib/math/add",
        "(a: number, b: number) -> {sum: number}",
        nlohmann::json::parse(R"({"type":"object","properties":{"sum":{"type":"number"}}})"),
        {},
        true,
        "Workflow"
    });
}
```

---

### Phase 4：pybind11 Python 绑定

**目标：** 通过 pybind11 暴露最小化 C++ 接口，Python 层不含任何业务逻辑。

#### 4.1 CMakeLists.txt 修改

在 `CMakeLists.txt` 中增加 pybind11 支持：

```cmake
# 在 cmake_minimum_required 之后增加
option(AGENTICDSL_BUILD_PYTHON "Build Python bindings" OFF)

if(AGENTICDSL_BUILD_PYTHON)
    find_package(pybind11 REQUIRED)

    pybind11_add_module(agentic_dsl_runtime
        src/modules/bindings/python.cpp
    )

    target_link_libraries(agentic_dsl_runtime PRIVATE
        agenticdsl_core
        ${LLAMA_LIB}
        yaml-cpp
    )

    # 设置输出目录（方便 pip install）
    set_target_properties(agentic_dsl_runtime PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/python/agentic_dsl_runtime"
    )
endif()
```

#### 4.2 pybind11 绑定实现

**新增 `src/modules/bindings/python.cpp`：**

```cpp
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include "core/engine.h"
#include "core/types/budget.h"
#include "core/types/node.h"

namespace py = pybind11;
using namespace agenticdsl;

// JSON <-> Python dict 转换辅助
nlohmann::json py_dict_to_json(const py::dict& d) {
    return nlohmann::json::parse(py::module_::import("json")
        .attr("dumps")(d).cast<std::string>());
}

py::dict json_to_py_dict(const nlohmann::json& j) {
    return py::module_::import("json")
        .attr("loads")(j.dump()).cast<py::dict>();
}

PYBIND11_MODULE(agentic_dsl_runtime, m) {
    m.doc() = "AgenticDSL Runtime - L0 C++ core engine (Thin Python Wrapper)";

    // ExecutionResult
    py::class_<ExecutionResult>(m, "ExecutionResult")
        .def_property_readonly("success", [](const ExecutionResult& r) { return r.success; })
        .def_property_readonly("message", [](const ExecutionResult& r) { return r.message; })
        .def_property_readonly("final_context",
            [](const ExecutionResult& r) { return json_to_py_dict(r.final_context); })
        .def_property_readonly("paused_at",
            [](const ExecutionResult& r) -> py::object {
                if (r.paused_at) return py::str(*r.paused_at);
                return py::none();
            });

    // TraceRecord
    py::class_<TraceRecord>(m, "TraceRecord")
        .def_property_readonly("node_path", [](const TraceRecord& t) { return t.node_path; })
        .def_property_readonly("status", [](const TraceRecord& t) { return t.status; })
        .def_property_readonly("context_delta",
            [](const TraceRecord& t) { return json_to_py_dict(t.context_delta); })
        .def_property_readonly("budget_snapshot",
            [](const TraceRecord& t) { return json_to_py_dict(t.budget_snapshot); });

    // DSLEngine - Thin Wrapper，不暴露内部实现细节
    py::class_<DSLEngine>(m, "DSLEngine")
        .def_static("from_markdown", &DSLEngine::from_markdown,
            py::arg("markdown_content"),
            "Parse and compile a DSL string")
        .def_static("from_file", &DSLEngine::from_file,
            py::arg("file_path"),
            "Parse and compile a DSL file")
        .def("run",
            [](DSLEngine& self, const py::dict& context) {
                py::gil_scoped_release release; // 释放 GIL（C++ 执行不阻塞 Python）
                return self.run(py_dict_to_json(context));
            },
            py::arg("context") = py::dict{},
            "Execute the compiled DSL workflow")
        .def("register_tool",
            [](DSLEngine& self, const std::string& name, py::function fn) {
                self.register_tool(name,
                    [fn](const std::unordered_map<std::string, std::string>& args)
                    -> nlohmann::json {
                        py::gil_scoped_acquire acquire; // 调用 Python 函数时重新获取 GIL
                        py::dict py_args;
                        for (const auto& [k, v] : args) py_args[k.c_str()] = v;
                        py::object result = fn(py_args);
                        // 支持返回 dict、str、int、bool
                        if (py::isinstance<py::dict>(result)) {
                            return py_dict_to_json(result.cast<py::dict>());
                        }
                        return nlohmann::json(result.cast<std::string>());
                    });
            },
            py::arg("name"), py::arg("handler"),
            "Register a Python function as a DSL tool")
        .def("get_last_traces", &DSLEngine::get_last_traces,
            "Get execution traces from the last run");

    // 版本信息
    m.attr("__version__") = "0.1.0";
    m.attr("__runtime_version__") = "2.2.0";
}
```

#### 4.3 pyproject.toml

**新增 `pyproject.toml`：**

```toml
[build-system]
requires = ["scikit-build-core>=0.4", "pybind11>=2.11"]
build-backend = "scikit_build_core.build"

[project]
name = "agentic-dsl-runtime"
version = "0.1.0"
description = "AgenticOS Layer 0 DSL Runtime - C++ core engine Python bindings"
requires-python = ">=3.10"
license = {text = "MIT"}

[tool.scikit-build]
cmake.args = ["-DAGENTICDSL_BUILD_PYTHON=ON"]
cmake.build-type = "Release"

[project.optional-dependencies]
dev = ["pytest>=7.0"]
```

#### 4.4 Python 绑定测试

**新增 `tests/test_bindings.py`：**

```python
"""测试 pybind11 Thin Wrapper 接口（不包含业务逻辑）"""
import pytest

try:
    import agentic_dsl_runtime as runtime
    HAS_RUNTIME = True
except ImportError:
    HAS_RUNTIME = False

SIMPLE_DSL = """
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/assign"]
  - id: assign
    type: assign
    assign:
      result: "hello from dsl"
    next: ["/main/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
"""

@pytest.mark.skipif(not HAS_RUNTIME, reason="C++ runtime not built")
def test_from_markdown_and_run():
    engine = runtime.DSLEngine.from_markdown(SIMPLE_DSL)
    result = engine.run({})
    assert result.success
    assert result.final_context["result"] == "hello from dsl"

@pytest.mark.skipif(not HAS_RUNTIME, reason="C++ runtime not built")
def test_register_python_tool():
    engine = runtime.DSLEngine.from_markdown("""
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/call_tool"]
  - id: call_tool
    type: tool_call
    tool: py_tool
    arguments: {x: "42"}
    output_keys: ["result"]
    next: ["/main/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
""")
    engine.register_tool("py_tool", lambda args: {"result": int(args["x"]) * 2})
    result = engine.run({})
    assert result.success
    assert result.final_context["result"] == 84

@pytest.mark.skipif(not HAS_RUNTIME, reason="C++ runtime not built")
def test_execution_traces():
    engine = runtime.DSLEngine.from_markdown(SIMPLE_DSL)
    engine.run({})
    traces = engine.get_last_traces()
    assert len(traces) > 0
    assert any(t.node_path == "/main/start" for t in traces)
```

---

### Phase 5：智能化演进特性（v2.2 核心）

**目标：** 实现 AgenticOS v2.2 的核心智能化特性：Layer Profile 编译时验证、智能调度、自适应预算、state 工具路由、风险感知人机协作。

#### 5.1 `SemanticValidator`（Layer Profile 编译时验证）

**新增 `src/modules/parser/semantic_validator.h`：**

```cpp
#pragma once
#include "modules/parser/markdown_parser.h"
#include "core/types/node.h"
#include <vector>
#include <string>

namespace agenticdsl {

class SemanticValidator {
public:
    explicit SemanticValidator(const std::vector<ParsedGraph>& graphs);

    // 运行所有验证，失败抛出 DSLError
    void validate();

private:
    const std::vector<ParsedGraph>& graphs_;

    // 验证子图命名空间与 layer_profile 匹配
    // /lib/cognitive/** → Cognitive
    // /lib/thinking/**  → Thinking
    // /lib/workflow/**  → Workflow
    void validate_namespace_profile_mapping();

    // 验证 Cognitive Profile 下 tool_call 只允许 state.read/state.write
    void validate_cognitive_tool_restrictions();

    // 验证 Thinking Profile 下禁止 state.write
    void validate_thinking_tool_restrictions();

    // 验证 state.read/write 工具在 __meta__/resources 中声明
    void validate_state_tool_declarations();

    // 验证所有 next/wait_for 引用的节点存在
    void validate_node_references();

    // 帮助方法：从节点路径推断所在子图的 layer_profile
    std::string infer_layer_profile(const NodePath& path) const;
};

} // namespace agenticdsl
```

**`SemanticValidator` 核心实现（`src/modules/parser/semantic_validator.cpp`）：**

```cpp
void SemanticValidator::validate_namespace_profile_mapping() {
    // 图路径 → 期望 Profile 的映射规则
    static const std::vector<std::pair<std::string, std::string>> rules = {
        {"/lib/cognitive/", "Cognitive"},
        {"/lib/thinking/",  "Thinking"},
        {"/lib/workflow/",  "Workflow"},
    };

    for (const auto& graph : graphs_) {
        for (const auto& [prefix, expected_profile] : rules) {
            if (graph.path.rfind(prefix, 0) == 0) {
                // 图路径匹配前缀，检查 /__meta__ 中的 layer_profile 声明
                std::string declared_profile = "Workflow"; // 默认
                for (const auto& g : graphs_) {
                    if (g.path == "/__meta__" && g.metadata.contains("layer_profile")) {
                        declared_profile = g.metadata["layer_profile"].get<std::string>();
                    }
                }
                if (declared_profile != expected_profile) {
                    throw DSLError(
                        ErrorCode::LAYER_PROFILE_VIOLATION,
                        "Graph " + graph.path + " requires " + expected_profile +
                        " profile, but declared " + declared_profile
                    );
                }
                break;
            }
        }
    }
}

void SemanticValidator::validate_cognitive_tool_restrictions() {
    for (const auto& graph : graphs_) {
        if (graph.path.rfind("/lib/cognitive/", 0) != 0) continue;
        for (const auto& node : graph.nodes) {
            if (node->type != NodeType::TOOL_CALL) continue;
            const auto* tool_node = static_cast<const ToolCallNode*>(node.get());
            if (tool_node->tool_name != "state.read" &&
                tool_node->tool_name != "state.write") {
                throw DSLError(
                    ErrorCode::LAYER_PROFILE_VIOLATION,
                    "Cognitive Profile node " + node->path +
                    " cannot call tool '" + tool_node->tool_name +
                    "' (only state.read/state.write allowed)"
                );
            }
        }
    }
}

void SemanticValidator::validate_thinking_tool_restrictions() {
    for (const auto& graph : graphs_) {
        if (graph.path.rfind("/lib/thinking/", 0) != 0) continue;
        for (const auto& node : graph.nodes) {
            if (node->type != NodeType::TOOL_CALL) continue;
            const auto* tool_node = static_cast<const ToolCallNode*>(node.get());
            if (tool_node->tool_name == "state.write") {
                throw DSLError(
                    ErrorCode::LAYER_PROFILE_VIOLATION,
                    "Thinking Profile node " + node->path +
                    " cannot use state.write (read-only access)"
                );
            }
        }
    }
}
```

**集成到 `DSLEngine::compile()`（Phase 1 新增的纯函数接口）：**

```cpp
std::vector<ParsedGraph> DSLEngine::compile(const std::string& markdown_content) {
    MarkdownParser parser;
    auto graphs = parser.parse_from_string(markdown_content);

    // Phase 5 新增：语义验证
    SemanticValidator validator(graphs);
    validator.validate(); // 编译时检查，失败抛出 DSLError

    bool has_main = false;
    for (const auto& g : graphs) {
        if (g.path == "/main") { has_main = true; break; }
    }
    if (!has_main) throw std::runtime_error("Required /main subgraph not found");
    return graphs;
}
```

**Layer Profile 验证测试（新增 `tests/test_layer_profile.cpp`）：**

```cpp
#include "catch_amalgamated.hpp"
#include "modules/parser/semantic_validator.h"
#include "modules/parser/markdown_parser.h"

TEST_CASE("Cognitive Profile rejects non-state tool calls", "[layer_profile]") {
    std::string dsl = R"(
### AgenticDSL `/__meta__`
```yaml
# --- BEGIN AgenticDSL ---
layer_profile: Cognitive
# --- END AgenticDSL ---
```

### AgenticDSL `/lib/cognitive/bad_node`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/lib/cognitive/bad_node/call"]
  - id: call
    type: tool_call
    tool: web_search  # ← 违规：Cognitive Profile 不允许
    arguments: {query: "test"}
    output_keys: ["result"]
    next: ["/lib/cognitive/bad_node/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
)";
    agenticdsl::MarkdownParser parser;
    auto graphs = parser.parse_from_string(dsl);
    agenticdsl::SemanticValidator validator(graphs);
    REQUIRE_THROWS_AS(validator.validate(), agenticdsl::DSLError);
}

TEST_CASE("Thinking Profile rejects state.write", "[layer_profile]") {
    // ... 类似测试结构 ...
}

TEST_CASE("Workflow Profile allows any tool", "[layer_profile]") {
    // ... 验证无违规 ...
}
```

---

#### 5.2 智能调度（`metadata.priority`）

**问题：** 当前 `TopoScheduler` 使用 `std::queue<NodePath>`（FIFO），无法按优先级排序。

**修改 `src/modules/scheduler/topo_scheduler.h`：**

```cpp
#include <queue>
#include <functional>

namespace agenticdsl {

// 新增：带优先级的节点队列元素
struct PriorityNode {
    NodePath path;
    int priority;

    // 最大堆：priority 大的先出队
    bool operator<(const PriorityNode& other) const {
        return priority < other.priority;
    }
};

class TopoScheduler {
    // ...
private:
    // 修改：将 std::queue<NodePath> 替换为优先级队列
    std::priority_queue<PriorityNode> ready_queue_; // ← 替换原来的 std::queue

    // 新增：从节点 metadata 读取优先级
    int get_node_priority(const Node* node) const {
        if (node->metadata.contains("priority") &&
            node->metadata["priority"].is_number_integer()) {
            return node->metadata["priority"].get<int>();
        }
        return 0; // 默认优先级
    }
};

} // namespace agenticdsl
```

**修改 `src/modules/scheduler/topo_scheduler.cpp` 中所有 `ready_queue_` 操作：**

```cpp
// build_dag() 中：
// 修改前：
ready_queue_.push(path);
// 修改后：
ready_queue_.push({path, get_node_priority(node_ptr.get())});

// execute() 中取队首：
// 修改前：
current_path = ready_queue_.front();
ready_queue_.pop();
// 修改后：
current_path = ready_queue_.top().path;
ready_queue_.pop();

// 后继节点入队（in_degree 归零时）：
// 修改前：
ready_queue_.push(next_path);
// 修改后：
Node* next_node = node_map_.at(next_path);
ready_queue_.push({next_path, get_node_priority(next_node)});
```

**DSL 使用示例：**

```yaml
### AgenticDSL `/main/critical_check`
```yaml
# --- BEGIN AgenticDSL ---
type: tool_call
tool: health_check
metadata:
  priority: 10        # 高优先级，相同依赖层中优先执行
  critical_path: true
arguments: {}
output_keys: ["health_status"]
next: ["/main/proceed"]
# --- END AgenticDSL ---
```

---

#### 5.3 自适应预算（`AdaptiveBudgetCalculator`）

**新增 `src/modules/budget/adaptive_budget.h`：**

```cpp
#pragma once
#include "core/types/budget.h"
#include "core/types/context.h"

namespace agenticdsl {

class AdaptiveBudgetCalculator {
public:
    // 基于置信度分数计算子图预算比例
    // ratio = 0.3 + 0.4 * confidence_score  (range: [0.3, 0.7])
    // 例：confidence=0.9 → ratio=0.66，confidence=0.3 → ratio=0.42
    static float compute_ratio(float confidence_score);

    // 从父预算和置信度创建子图预算
    // 子图 max_nodes = parent.max_nodes * ratio
    // 性能要求：< 1ms
    static ExecutionBudget create_child_budget(
        const ExecutionBudget& parent,
        float confidence_score);

    // 从 Context 中推断置信度分数（读取 context["confidence_score"]）
    // 如果不存在，返回默认值 0.5
    static float extract_confidence(const Context& ctx);
};

} // namespace agenticdsl
```

**实现（`src/modules/budget/adaptive_budget.cpp`）：**

```cpp
float AdaptiveBudgetCalculator::compute_ratio(float confidence_score) {
    // 限制到 [0.0, 1.0]
    confidence_score = std::clamp(confidence_score, 0.0f, 1.0f);
    // 线性映射到 [0.3, 0.7]
    return 0.3f + 0.4f * confidence_score;
}

ExecutionBudget AdaptiveBudgetCalculator::create_child_budget(
    const ExecutionBudget& parent, float confidence_score) {

    float ratio = compute_ratio(confidence_score);
    ExecutionBudget child;

    // 按比例分配，-1 表示无限制（继承无限制）
    if (parent.max_nodes >= 0) {
        child.max_nodes = static_cast<int>(parent.max_nodes * ratio);
        child.max_nodes = std::max(child.max_nodes, 1); // 至少 1 个节点
    } else {
        child.max_nodes = -1;
    }

    if (parent.max_llm_calls >= 0) {
        child.max_llm_calls = static_cast<int>(parent.max_llm_calls * ratio);
        child.max_llm_calls = std::max(child.max_llm_calls, 1);
    } else {
        child.max_llm_calls = -1;
    }

    if (parent.max_duration_sec >= 0) {
        child.max_duration_sec = static_cast<int>(parent.max_duration_sec * ratio);
        child.max_duration_sec = std::max(child.max_duration_sec, 1);
    } else {
        child.max_duration_sec = -1;
    }

    child.max_subgraph_depth = (parent.max_subgraph_depth >= 0)
        ? parent.max_subgraph_depth - 1  // 深度递减
        : -1;

    return child;
}

float AdaptiveBudgetCalculator::extract_confidence(const Context& ctx) {
    if (ctx.contains("confidence_score") && ctx["confidence_score"].is_number()) {
        return ctx["confidence_score"].get<float>();
    }
    return 0.5f; // 默认置信度
}
```

**集成到 `GenerateSubgraphNode` 执行（修改 `execute_generate_subgraph`）：**

```cpp
// 在 execute_generate_subgraph 中，为动态子图创建自适应预算
Context NodeExecutor::execute_generate_subgraph(
    const GenerateSubgraphNode* node, const Context& ctx) {

    // ... 现有 LLM 调用和解析逻辑 ...

    // Phase 5 新增：从上下文提取置信度，创建子图自适应预算
    float confidence = AdaptiveBudgetCalculator::extract_confidence(ctx);
    // 子图预算从 ExecutionSession 的当前预算计算（需传入引用）
    // 暂存到 context 中供调度器使用
    new_context["__child_budget_confidence__"] = confidence;

    // ... 其余逻辑 ...
}
```

**测试（新增 `tests/test_adaptive_budget.cpp`）：**

```cpp
#include "catch_amalgamated.hpp"
#include "modules/budget/adaptive_budget.h"

TEST_CASE("AdaptiveBudgetCalculator compute_ratio", "[budget][adaptive]") {
    REQUIRE(agenticdsl::AdaptiveBudgetCalculator::compute_ratio(0.0f) == Catch::Approx(0.3f));
    REQUIRE(agenticdsl::AdaptiveBudgetCalculator::compute_ratio(1.0f) == Catch::Approx(0.7f));
    REQUIRE(agenticdsl::AdaptiveBudgetCalculator::compute_ratio(0.5f) == Catch::Approx(0.5f));
    // 边界值：超出 [0,1] 被 clamp
    REQUIRE(agenticdsl::AdaptiveBudgetCalculator::compute_ratio(-0.5f) == Catch::Approx(0.3f));
    REQUIRE(agenticdsl::AdaptiveBudgetCalculator::compute_ratio(1.5f) == Catch::Approx(0.7f));
}

TEST_CASE("AdaptiveBudgetCalculator create_child_budget", "[budget][adaptive]") {
    agenticdsl::ExecutionBudget parent;
    parent.max_nodes = 50;
    parent.max_llm_calls = 10;
    parent.max_duration_sec = 60;
    parent.max_subgraph_depth = 3;

    auto child = agenticdsl::AdaptiveBudgetCalculator::create_child_budget(parent, 0.5f);

    REQUIRE(child.max_nodes == 25);       // 50 * 0.5 = 25
    REQUIRE(child.max_llm_calls == 5);    // 10 * 0.5 = 5
    REQUIRE(child.max_duration_sec == 30);// 60 * 0.5 = 30
    REQUIRE(child.max_subgraph_depth == 2);// 3 - 1 = 2
}

TEST_CASE("AdaptiveBudgetCalculator extract_confidence from context", "[budget][adaptive]") {
    agenticdsl::Context ctx;
    ctx["confidence_score"] = 0.8f;
    REQUIRE(agenticdsl::AdaptiveBudgetCalculator::extract_confidence(ctx) == Catch::Approx(0.8f));

    agenticdsl::Context empty_ctx;
    REQUIRE(agenticdsl::AdaptiveBudgetCalculator::extract_confidence(empty_ctx) == Catch::Approx(0.5f));
}
```

---

#### 5.4 `state.read`/`state.write` 工具路由

**目标：** `NodeExecutor::execute_tool_call` 对 `state.read`/`state.write` 工具进行特殊路由，通过 `ToolRegistry` 调用（L2 注册的实现）并在运行时验证权限。

**修改 `src/modules/executor/node_executor.cpp`，增加路由逻辑：**

```cpp
Context NodeExecutor::execute_tool_call(const ToolCallNode* node, const Context& ctx) {
    Context new_context = ctx;
    const auto& tool_name = node->tool_name;

    // state 工具：验证权限声明
    if (tool_name == "state.read" || tool_name == "state.write") {
        // 检查节点是否声明了对应权限
        std::string required_perm = "state:" + tool_name; // e.g., "state:state.read"
        bool has_perm = std::find(node->permissions.begin(),
                                  node->permissions.end(),
                                  required_perm) != node->permissions.end();
        if (!has_perm) {
            throw DSLError(
                ErrorCode::TOOL_PERMISSION_DENIED,
                "Node " + node->path + " requires permission '" + required_perm +
                "' to use " + tool_name
            );
        }

        // 验证工具已注册（L2 必须提前注册）
        if (!tool_registry_.has_tool(tool_name)) {
            throw DSLError(
                ErrorCode::STATE_TOOL_NOT_REGISTERED,
                tool_name + " is not registered. L2 must register it via register_tool()."
            );
        }
    }

    // 渲染参数（Inja 模板）
    std::unordered_map<std::string, std::string> rendered_args;
    for (const auto& [key, tmpl] : node->arguments) {
        rendered_args[key] = InjaTemplateRenderer::render(tmpl, ctx);
    }

    // 调用工具（state.read/write 走同一 ToolRegistry，L2 注入实现）
    if (!tool_registry_.has_tool(tool_name)) {
        throw DSLError(ErrorCode::TOOL_NOT_FOUND,
                       "Tool '" + tool_name + "' not registered");
    }
    nlohmann::json result = tool_registry_.call_tool(tool_name, rendered_args);

    // 处理 output_keys
    if (node->output_keys.size() == 1) {
        new_context[node->output_keys[0]] = result;
    } else if (result.is_object()) {
        for (const auto& key : node->output_keys) {
            if (result.contains(key)) new_context[key] = result[key];
        }
    } else if (!node->output_keys.empty()) {
        new_context[node->output_keys[0]] = result;
    }

    return new_context;
}
```

**DSL 使用示例（`state.read`）：**

```yaml
### AgenticDSL `/lib/cognitive/read_user_profile`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
signature: "(user_id: string) -> {profile: object}"
permissions: ["state:state.read"]
nodes:
  - id: start
    type: start
    next: ["/lib/cognitive/read_user_profile/read"]
  - id: read
    type: tool_call
    tool: state.read
    permissions: ["state:state.read"]
    arguments:
      key: "user_profile:{{ user_id }}"
    output_keys: ["profile"]
    next: ["/lib/cognitive/read_user_profile/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```

**L2 中注册 state 工具（宿主程序示例，参考 `examples/agent_basic/main.cpp`）：**

```cpp
auto engine = agenticdsl::DSLEngine::from_file("workflow.md");

// L2 注入 state 工具实现（CognitiveStateManager 来自 AgenticOS L4）
engine->register_tool("state.read", [&state_manager](const auto& args) {
    std::string key = args.at("key");
    return state_manager.read(key); // 返回 JSON
});

engine->register_tool("state.write", [&state_manager](const auto& args) {
    std::string key = args.at("key");
    std::string value = args.at("value");
    state_manager.write(key, value);
    return nlohmann::json{{"success", true}};
});
```

**测试（新增 `tests/test_state_tool.cpp`）：**

```cpp
#include "catch_amalgamated.hpp"
#include "core/engine.h"

TEST_CASE("state.read tool registered and callable", "[state_tool]") {
    std::string dsl = R"(
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/read_state"]
  - id: read_state
    type: tool_call
    tool: state.read
    permissions: ["state:state.read"]
    arguments: {key: "test_key"}
    output_keys: ["value"]
    next: ["/main/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
)";
    auto engine = agenticdsl::DSLEngine::from_markdown(dsl);
    // 注册 mock state.read
    engine->register_tool("state.read", [](const auto& args) -> nlohmann::json {
        return {{"value", "mock_state_value"}};
    });

    auto result = engine->run({});
    REQUIRE(result.success);
    REQUIRE(result.final_context["value"] == nlohmann::json{{"value", "mock_state_value"}});
}

TEST_CASE("state.read without permission declaration throws", "[state_tool]") {
    std::string dsl = R"(
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/read_state"]
  - id: read_state
    type: tool_call
    tool: state.read
    # permissions: 未声明 → 应抛出权限错误
    arguments: {key: "test_key"}
    output_keys: ["value"]
    next: ["/main/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
)";
    auto engine = agenticdsl::DSLEngine::from_markdown(dsl);
    engine->register_tool("state.read", [](const auto&) -> nlohmann::json {
        return {{"value", "x"}};
    });
    auto result = engine->run({});
    REQUIRE_FALSE(result.success); // 权限检查失败
}
```

---

#### 5.5 风险感知人机协作（`require_human_approval: risk_based`）

**目标：** 当节点 `metadata.risk_level` 超过阈值时，暂停执行并通知宿主程序（L2）进行人工审批。

**设计：** 利用现有的 `ExecutionResult::paused_at` 字段（已被 LLM 调用暂停使用），扩展为支持人工审批暂停。

**修改 `src/core/types/budget.h`（扩展 `ExecutionResult`）：**

```cpp
struct ExecutionResult {
    bool success;
    std::string message;
    Context final_context;
    std::optional<NodePath> paused_at; // LLM 暂停 或 人工审批暂停
    enum class PauseReason { NONE, LLM_CALL, HUMAN_APPROVAL } pause_reason = PauseReason::NONE;
    float risk_score = 0.0f; // 暂停时的风险分数
};
```

**在 `ExecutionSession::execute_node` 中，LLM_CALL 节点执行前检查风险：**

```cpp
// Phase 5 新增：风险感知人机协作检查
if (current_node->metadata.contains("risk_level")) {
    float risk = current_node->metadata["risk_level"].get<float>();
    float threshold = 0.7f; // 默认阈值（可从 /__meta__ 配置）

    if (risk >= threshold) {
        // require_human_approval: risk_based → 暂停执行
        return {context, false,
                "Human approval required for high-risk node: " + current_node->path,
                std::nullopt, std::nullopt};
        // paused_at 由调度器设置，pause_reason = HUMAN_APPROVAL
    }
}
```

**DSL 使用示例：**

```yaml
### AgenticDSL `/main/delete_user`
```yaml
# --- BEGIN AgenticDSL ---
type: tool_call
tool: db_delete
metadata:
  risk_level: 0.9        # 高风险操作
  description: "不可逆：删除用户数据"
permissions: ["db:write"]
arguments:
  user_id: "{{ user_id }}"
output_keys: ["deleted"]
next: ["/main/end"]
# --- END AgenticDSL ---
```

**宿主程序（L2）处理暂停：**

```cpp
auto result = engine->run(context);
while (result.paused_at.has_value() &&
       result.pause_reason == ExecutionResult::PauseReason::HUMAN_APPROVAL) {
    std::cout << "Human approval required for: " << *result.paused_at
              << " (risk=" << result.risk_score << ")" << std::endl;

    // 等待人工确认（通过 UI/API）
    bool approved = wait_for_human_decision(*result.paused_at);
    if (!approved) {
        // 人工拒绝，终止执行
        break;
    }
    // 人工批准，继续执行
    result = engine->continue_from(*result.paused_at, result.final_context);
}
```

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
