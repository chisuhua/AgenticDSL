# AgenticDSL 参考执行器 v1.0 实现框架

> **目标**：提供一个**可验证、可嵌入、可扩展**的参考实现，完整支持 AgenticDSL v3.10+ 规范的核心能力，强化 **三层架构隔离、资源声明联动、C++ 推理内核集成、动态子图生成** 四大特性。v3.9 规范中的**安全归档**（`archive_to` 签名强制校验）当前为部分实现，计划在后续迭代中完善。

## 一、总体架构

执行器采用 **分层 + 模块化** 设计，所有模块通过清晰接口交互，无全局状态。

```
+---------------------------------------------+
|              DSLEngine (engine.h)            |
|   from_markdown() / from_file() / run()      |
+--------------------+------------------------+
                     |
         +-----------v-----------+
         |     TopoScheduler     |
         |   (topo_scheduler.h)  |
         +----+----------+-------+
              |          |
   +----------v--+  +----v-----------+
   |ExecutionSess|  | ResourceManager|
   |ion(.h/.cpp) |  | (resource_mgr) |
   +---+----+----+  +----------------+
       |    |
  +----v-+  +------v------+
  |Budget|  |ContextEngine|  (context_engine.h)
  |Ctrl  |  +------+------+
  +------+         |
       |       +---v---+
  +----v----+  |Trace  |
  |NodeExec |  |Export |  (trace_exporter.h)
  |utor     |  +-------+
  +-+----+--+
    |    |
+---v-+  +-------v-------+
|Tool |  |  LlamaAdapter  |  (llama_adapter.h)
|Reg. |  +---------------+
+--+--+
   |
+--v----------+
|MarkdownParser|  (markdown_parser.h)
+-------------+
```

### 目录结构

```
src/
├── core/
│   ├── engine.h / engine.cpp          ← 用户入口
│   └── types/
│       ├── node.h                     ← 节点类型与 ParsedGraph
│       ├── context.h                  ← Context = nlohmann::json
│       ├── budget.h                   ← ExecutionBudget 与原子计数器
│       └── resource.h                 ← Resource / ResourceType
├── common/
│   ├── llm/
│   │   ├── llm_tool.h                 ← ILLMTool 接口 / LLMParams / LLMResult
│   │   ├── llama_adapter.h/cpp        ← llama.cpp 封装适配器
│   │   └── llama_tool.h/cpp           ← ILLMTool 实现（基于 LlamaAdapter）
│   ├── tools/
│   │   └── registry.h/cpp             ← ToolRegistry（普通工具 + LLM 工具）
│   └── utils/
│       ├── template_renderer.h/cpp    ← Inja 模板渲染
│       ├── parser_utils.h/cpp         ← 路径校验工具
│       └── yaml_json.h/cpp            ← YAML ↔ JSON 转换
└── modules/
    ├── budget/
    │   └── budget_controller.h/cpp    ← 预算管理与强制执行
    ├── context/
    │   └── context_engine.h/cpp       ← 上下文合并策略与快照管理
    ├── executor/
    │   ├── node_executor.h/cpp        ← 节点类型调度执行
    │   └── node.cpp                   ← 节点实现辅助
    ├── library/
    │   ├── library_loader.h/cpp       ← 标准库加载（单例）
    │   └── schema.h                   ← LibraryEntry 定义
    ├── parser/
    │   └── markdown_parser.h/cpp      ← Markdown DSL 解析 → ParsedGraph
    ├── scheduler/
    │   ├── topo_scheduler.h/cpp       ← 拓扑排序调度器
    │   ├── execution_session.h/cpp    ← 单次执行状态封装
    │   └── resource_manager.h/cpp     ← 资源注册与查询
    ├── system/
    │   └── system_nodes.h/cpp         ← 内置系统节点
    └── trace/
        └── trace_exporter.h/cpp       ← OpenTelemetry 兼容 Trace
```

### 关键设计原则
- **无跨层调用**：`NodeExecutor` 仅通过 `ToolRegistry`/`LlamaAdapter` 访问外部，不直接调用调度器或上下文引擎。
- **数据驱动**：所有状态通过 `Context`（`nlohmann::json`）传递，无全局变量。
- **预算前置**：所有资源消耗（节点、LLM 调用、子图深度）在执行前通过 `BudgetController` 检查。
- **解析即验证**：`MarkdownParser` 在解析时完成路径、签名、资源声明的基本校验。
- **依赖注入**：`ToolRegistry` 和 `LlamaAdapter` 通过构造参数注入各子模块，无单例依赖（`StandardLibraryLoader` 除外）。

## 二、核心模块定义

### 2.1 `DSLEngine`（用户接口层）

**职责**：DSL 文档加载、执行启动、动态图追加、工具注册。

**头文件**：`src/core/engine.h`，命名空间 `agenticdsl`

```cpp
class DSLEngine {
public:
    // 工厂方法
    static std::unique_ptr<DSLEngine> from_markdown(const std::string& markdown_content);
    static std::unique_ptr<DSLEngine> from_file(const std::string& file_path);

    // 执行
    ExecutionResult run(const Context& context = Context{});

    // 动态 DSL 注入（供 LLM 回调使用）
    void continue_with_generated_dsl(const std::string& generated_dsl);
    void append_graphs(std::vector<ParsedGraph> new_graphs);

    // 普通工具注册（模板化，支持任意可调用对象）
    template <typename Func>
    void register_tool(std::string_view name, Func&& func);

    // LLM 工具注册（依赖注入，支持 ILLMTool 实现）
    void register_llm_tool(std::string name,
                           std::unique_ptr<ILLMTool> tool,
                           const LLMParams& default_params = {});

    // 访问器
    ToolRegistry& get_tool_registry();
    const ToolRegistry& get_tool_registry() const;
    LlamaAdapter* get_llm_adapter();
    std::vector<TraceRecord> get_last_traces() const;

    // 构造（通常通过工厂方法创建）
    explicit DSLEngine(std::vector<ParsedGraph> initial_graphs);

private:
    std::vector<ParsedGraph> full_graphs_;
    ToolRegistry tool_registry_;              // 成员变量，非单例
    std::unique_ptr<LlamaAdapter> llama_adapter_;
    std::vector<TraceRecord> last_traces_;
};
```

**注意点**：
- `from_markdown` / `from_file` 内部调用 `MarkdownParser`，完成解析后构造 `DSLEngine`。
- `continue_with_generated_dsl` 仅接受符合 `### AgenticDSL '/dynamic/...'` 格式的输入；内部转发至 `append_graphs`。
- `ToolRegistry` 是引擎的成员变量，每个引擎实例独立管理工具，**不使用全局单例**。

---

### 2.2 类型系统（`src/core/types/`）

#### Context（`context.h`）
```cpp
using Value   = nlohmann::json;  // 统一的值类型
using Context = nlohmann::json;  // 执行上下文（JSON 对象）
```

#### NodeType 枚举（`node.h`）
```cpp
enum class NodeType : uint8_t {
    START,             // 入口节点
    END,               // 终止节点（hard/soft 两种模式）
    ASSIGN,            // 变量赋值（含 Inja 模板渲染）
    DSL_CALL,          // LLM 驱动的 DSL 生成（v3.10 新增，替代 LLM_CALL）
    TOOL_CALL,         // 调用已注册工具
    RESOURCE,          // 资源声明与注册
    FORK,              // 并行分支（v3.1）
    JOIN,              // 同步屏障（v3.1）
    GENERATE_SUBGRAPH, // 运行时动态 DSL 生成（v3.1）
    ASSERT             // 条件断言
};
```

> **v3.10 变更**：`LLM_CALL` 已重命名为 `DSL_CALL`，对应节点类型由 `LLMCallNode`（已弃用）变为 `DSLNode`。`DSL_CALL` 的语义更精确——专指**通过 LLM 工具生成文本/DSL 内容**，并支持通过 `llm_tool_name` 字段选择具体的 LLM 后端（如 `"llama-7b"`），而旧版 `LLMCallNode` 仅支持 `prompt_template` + `output_keys` 的简化调用方式。已有 `llm_call` 节点的 DSL 文档可继续运行，但建议迁移到 `dsl_call` 格式以获得完整的参数控制能力（`llm_params`、多后端切换等）。

#### 节点基类（`node.h`）
```cpp
struct Node {
    NodePath path;                          // 节点完整路径，如 "/main/step1"
    NodeType type;
    std::vector<NodePath> next;             // 后继节点路径
    nlohmann::json metadata;                // 任意元数据
    std::optional<std::string> signature;   // 函数签名，如 "(input: string) -> {result: number}"
    std::vector<std::string> permissions;   // 权限声明，如 ["network", "file:read"]

    virtual ~Node() = default;
    [[nodiscard]] virtual Context execute(Context& context) = 0;
    virtual std::unique_ptr<Node> clone() const = 0;
};
```

#### 具体节点类型汇总

| 节点类型 | 结构体 | 关键字段 |
|----------|--------|----------|
| `START` | `StartNode` | `next`（后继路径列表） |
| `END` | `EndNode` | `metadata.termination_mode`（`hard`/`soft`） |
| `ASSIGN` | `AssignNode` | `assign`（`map<key, Inja模板>`） |
| `DSL_CALL` | `DSLNode` | `prompt_template`, `llm_tool_name`, `llm_params`, `output_keys` |
| `DSL_CALL`（旧） | `LLMCallNode` ⚠️ | `prompt_template`, `output_keys`（**已弃用**，v3.10 前向兼容） |
| `TOOL_CALL` | `ToolCallNode` | `tool_name`, `arguments`, `output_keys` |
| `RESOURCE` | `ResourceNode` | `resource_type`, `uri`, `scope` |
| `FORK` | `ForkNode` | `branches`（分支路径列表） |
| `JOIN` | `JoinNode` | `wait_for`（依赖节点列表）, `merge_strategy` |
| `GENERATE_SUBGRAPH` | `GenerateSubgraphNode` | `prompt_template`, `output_keys`, `signature_validation` |
| `ASSERT` | `AssertNode` | `condition`（Inja 布尔表达式）, `on_failure` |

#### ParsedGraph（`node.h`）
```cpp
struct ParsedGraph {
    std::vector<std::unique_ptr<Node>> nodes;
    NodePath path;                          // 图路径，如 "/main"、"/lib/utils"
    nlohmann::json metadata;               // 图级元数据（含 entry_point、execution_budget 等）
    std::optional<ExecutionBudget> budget; // 从 /__meta__ 解析的执行预算
    std::optional<std::string> signature;  // 子图签名
    std::vector<std::string> permissions;  // 子图权限列表
    bool is_standard_library = false;      // 路径以 /lib/ 开头时为 true
    std::optional<nlohmann::json> output_schema; // v3.1：解析 signature.outputs 得到的 JSON Schema

    // 仅支持移动语义，禁止拷贝
    ParsedGraph() = default;
    ParsedGraph(ParsedGraph&&) = default;
    ParsedGraph& operator=(ParsedGraph&&) = default;
    ParsedGraph(const ParsedGraph&) = delete;
    ParsedGraph& operator=(const ParsedGraph&) = delete;
};
```

#### ExecutionBudget（`budget.h`）
```cpp
struct ExecutionBudget {
    int max_nodes         = -1;   // -1 表示无限制
    int max_llm_calls     = -1;
    int max_duration_sec  = -1;
    int max_subgraph_depth = -1;
    int max_snapshots     = -1;
    size_t snapshot_max_size_kb = 512;

    mutable std::atomic<int> nodes_used{0};
    mutable std::atomic<int> llm_calls_used{0};
    mutable std::atomic<int> subgraph_depth_used{0};
    std::chrono::steady_clock::time_point start_time;

    bool exceeded() const;
    bool try_consume_node();
    bool try_consume_llm_call();
    bool try_consume_subgraph_depth();
};

struct ExecutionResult {
    bool success;
    std::string message;
    Context final_context;
    std::optional<NodePath> paused_at;  // LLM 暂停点
};
```

#### Resource（`resource.h`）
```cpp
enum class ResourceType : uint8_t {
    FILE, POSTGRES, MYSQL, SQLITE,
    API_ENDPOINT, VECTOR_STORE, CUSTOM
};

struct Resource {
    NodePath path;              // 资源路径，如 "/__meta__/resources/db"
    ResourceType resource_type;
    std::string uri;
    std::string scope;          // "global" 或 "local"
    nlohmann::json metadata;    // 任意元数据（如 capabilities 列表）
};
```

---

### 2.3 `MarkdownParser`（DSL 解析器）

**头文件**：`src/modules/parser/markdown_parser.h`

```cpp
class MarkdownParser {
public:
    // 从字符串解析，返回所有解析到的子图（含 /lib/**、/__meta__ 等）
    std::vector<ParsedGraph> parse_from_string(const std::string& markdown_content);

    // 从文件路径解析
    std::vector<ParsedGraph> parse_from_file(const std::string& file_path);

    // 从 JSON 对象创建单个节点（供内部和测试使用）
    std::unique_ptr<Node> create_node_from_json(const NodePath& path,
                                                const nlohmann::json& node_json);
private:
    void validate_nodes(const std::vector<std::unique_ptr<Node>>& nodes);
    std::optional<nlohmann::json> parse_output_schema_from_signature(
        const std::string& signature_str);
};
```

**DSL Markdown 格式示例**：
```markdown
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/process"]
  - id: process
    type: assign
    assign:
      result: "{{ input | upper }}"
    output_keys: ["result"]
    next: ["/main/end"]
  - id: end
    type: end
    metadata:
      termination_mode: hard
# --- END AgenticDSL ---
```
```

---

### 2.4 `TopoScheduler`（调度核心）

**头文件**：`src/modules/scheduler/topo_scheduler.h`

```cpp
class TopoScheduler {
public:
    struct Config {
        std::optional<ExecutionBudget> initial_budget;
    };

    TopoScheduler(Config config, ToolRegistry& tool_registry,
                  LlamaAdapter* llm_adapter,
                  const std::vector<ParsedGraph>* full_graphs = nullptr);

    // 节点注册与 DAG 构建
    void register_node(std::unique_ptr<Node> node);
    void build_dag();

    // 执行（拓扑排序驱动）
    ExecutionResult execute(Context initial_context);

    // 动态图注入（供 continue_with_generated_dsl 使用）
    void append_dynamic_graphs(std::vector<ParsedGraph> new_graphs);

    // 获取执行 Trace
    std::vector<TraceRecord> get_last_traces() const;

private:
    const std::vector<ParsedGraph>* full_graphs_;
    ResourceManager resource_manager_;
    ExecutionSession session_;
    std::vector<ParsedGraph> dynamic_graphs_;

    // DAG 状态
    std::vector<std::unique_ptr<Node>> all_nodes_;
    std::unordered_map<NodePath, Node*> node_map_;
    std::unordered_map<NodePath, std::vector<NodePath>> reverse_edges_;
    std::unordered_map<NodePath, int> in_degree_;
    std::queue<NodePath> ready_queue_;
    std::unordered_set<NodePath> executed_;
    std::vector<NodePath> call_stack_;  // 用于 soft end 检测

    // Fork/Join 状态（v3.1 顺序模拟）
    std::optional<NodePath> current_fork_node_path_;
    std::vector<NodePath> current_fork_branches_;
    std::vector<Context> current_fork_branch_results_;
    size_t current_fork_branch_index_ = 0;
    bool is_executing_fork_branches_ = false;
    std::string join_merge_strategy_ = "error_on_conflict";
    std::vector<NodePath> join_wait_for_;
    std::optional<NodePath> current_join_node_path_;

    // 内部辅助方法
    void register_resources();
    void load_graphs(const std::vector<std::unique_ptr<Node>>& nodes);
    void start_fork_simulation(const ForkNode* fork_node, const Context& snapshot);
    void execute_fork_branches();
    Context execute_single_branch(const NodePath& branch_path, const Context& initial_context);
    void finish_fork_simulation();
    void start_join_simulation(const JoinNode* join_node);
    void finish_join_simulation(Context& main_context);
};
```

**调度策略**：
- **拓扑排序**：基于入度（in-degree）的就绪队列（BFS）驱动执行。
- **动态依赖**：`wait_for` 可含 Inja 表达式，在运行时根据上下文解析。
- **Fork/Join 模拟**：当前为**顺序执行**（非真正并发），各分支依次执行后由 JoinNode 合并上下文。
- **预算超限**：超限时跳转至 `/__system__/budget_exceeded` 节点（硬终止）。

---

### 2.5 `ExecutionSession`（执行会话）

**头文件**：`src/modules/scheduler/execution_session.h`

封装单次执行所需的所有横切关注点：预算、快照、Trace、权限检查。

```cpp
class ExecutionSession {
public:
    using AppendGraphsCallback = std::function<void(std::vector<ParsedGraph>)>;

    ExecutionSession(
        std::optional<ExecutionBudget> initial_budget,
        ToolRegistry& tool_registry,
        LlamaAdapter* llm_adapter,
        ResourceManager& resource_manager,
        const std::vector<ParsedGraph>* full_graphs,
        AppendGraphsCallback append_graphs_callback = nullptr
    );

    struct ExecutionResult {
        Context new_context;
        bool success;
        std::string message;
        std::optional<NodePath> snapshot_key;  // 本次执行触发的快照
        std::optional<NodePath> paused_at;     // LLM 暂停点
    };

    ExecutionResult execute_node(Node* node, const Context& initial_context);
    void check_and_requeue_dynamic_deps(
        const std::unordered_set<NodePath>& newly_executed_nodes);

    bool is_budget_exceeded() const;

    // 访问子组件（只读）
    const TraceExporter& get_trace_exporter() const;
    const BudgetController& get_budget_controller() const;
    const ContextEngine& get_context_engine() const;
    const std::unordered_map<NodePath, std::vector<NodePath>>&
        get_pending_dynamic_deps() const;
    const std::unordered_map<NodePath, nlohmann::json>&
        get_dynamic_wait_for_expressions() const;

private:
    ResourceManager& resource_manager_;
    ContextEngine context_engine_;
    BudgetController budget_controller_;
    TraceExporter trace_exporter_;
    NodeExecutor node_executor_;
    const std::vector<ParsedGraph>* full_graphs_;
    std::vector<NodePath> call_stack_;

    std::unordered_map<NodePath, std::vector<NodePath>> pending_dynamic_deps_;
    std::unordered_map<NodePath, nlohmann::json> dynamic_wait_for_expressions_;
    AppendGraphsCallback append_graphs_callback_;

    nlohmann::json build_available_subgraphs_context() const;
    std::string inject_subgraphs_into_prompt(const std::string& base_prompt,
                                              const Context& context) const;
    bool needs_snapshot(Node* node) const;
    std::vector<NodePath> parse_dynamic_wait_for(const nlohmann::json& expr,
                                                  const Context& ctx);

    friend class TopoScheduler;
};
```

**关键行为**：
- **自动快照**：在 `ForkNode`、`GenerateSubgraphNode`、`AssertNode` 执行前自动调用 `ContextEngine::save_snapshot`。
- **动态依赖**：`GenerateSubgraphNode` 执行后，通过 `AppendGraphsCallback` 将新图注入调度器，并更新 `pending_dynamic_deps_`。

---

### 2.6 `NodeExecutor`（节点执行器）

**头文件**：`src/modules/executor/node_executor.h`

```cpp
class NodeExecutor {
public:
    NodeExecutor(ToolRegistry& tool_registry, LlamaAdapter* llm_adapter = nullptr);

    Context execute_node(Node* node, const Context& ctx);
    void set_append_graphs_callback(AppendGraphsCallback cb);

private:
    ToolRegistry& tool_registry_;
    LlamaAdapter* llm_adapter_;
    AppendGraphsCallback append_graphs_callback_;
    MarkdownParser markdown_parser_;  // 内部用于解析动态生成的 DSL

    void check_permissions(const std::vector<std::string>& perms,
                           const NodePath& node_path);

    // 各节点类型的执行实现
    Context execute_start(const StartNode* node, const Context& ctx);
    Context execute_end(const EndNode* node, const Context& ctx);
    Context execute_assign(const AssignNode* node, const Context& ctx);
    Context execute_dsl_node(const DSLNode* node, const Context& ctx);        // v3.10
    Context execute_llm_call(const LLMCallNode* node, const Context& ctx);    // 已弃用，向后兼容
    Context execute_tool_call(const ToolCallNode* node, const Context& ctx);
    Context execute_resource(const ResourceNode* node, const Context& ctx);
    Context execute_fork(const ForkNode* node, const Context& ctx);
    Context execute_join(const JoinNode* node, const Context& ctx);
    Context execute_generate_subgraph(const GenerateSubgraphNode* node, const Context& ctx);
    Context execute_assert(const AssertNode* node, const Context& ctx);
};
```

**执行流程**：
1. `execute_node` 根据 `node->type` 分发到对应 `execute_*` 方法。
2. 各方法从 `ctx` 读取输入，渲染 Inja 模板，调用工具/LLM，将结果写回返回的新 `Context`。
3. `execute_dsl_node`：通过 `ToolRegistry::call_llm_tool` 调用指定 LLM 工具，将输出写入 `output_keys`。
4. `execute_generate_subgraph`：生成 DSL 字符串后，通过 `AppendGraphsCallback` 将解析后的图动态注入调度器。

---

### 2.7 `ContextEngine`（上下文引擎）

**头文件**：`src/modules/context/context_engine.h`

```cpp
using MergeStrategy = std::string;
// 支持的策略："error_on_conflict" | "last_write_wins" |
//              "deep_merge" | "array_concat" | "array_merge_unique"

struct ContextMergePolicy {
    std::unordered_map<std::string, MergeStrategy> field_policies; // 精确路径或通配符
    MergeStrategy default_strategy = "error_on_conflict";
};

class ContextEngine {
public:
    struct Result {
        Context new_context;
        std::optional<NodePath> snapshot_key;
    };

    // 执行节点函数并可选地保存快照（聚合接口）
    Result execute_with_snapshot(
        std::function<Context(const Context&)> execute_func,
        const Context& ctx,
        bool need_snapshot,
        const NodePath& snapshot_node_path
    );

    // 静态上下文合并（供 JoinNode 等使用）
    static void merge(Context& target, const Context& source,
                      const ContextMergePolicy& policy = {});

    // 快照管理（FIFO，受预算限制）
    void save_snapshot(const NodePath& key, const Context& ctx);
    const Context* get_snapshot(const NodePath& key) const;
    void enforce_snapshot_budget();
    void set_snapshot_limits(size_t max_count, size_t max_size_kb);

private:
    std::unordered_map<NodePath, Context> snapshots_;
    std::vector<NodePath> snapshot_order_;   // FIFO 顺序
    size_t max_snapshots_ = 10;
    size_t max_snapshot_size_kb_ = 512;
    size_t current_total_size_kb_ = 0;

    static void merge_recursive(Context& target, const Context& source,
                                const std::string& path_prefix,
                                const ContextMergePolicy& policy);
    static void merge_array(Context& target_arr, const Context& source_arr,
                            MergeStrategy strategy);
    static void merge_scalar(Context& target_val, const Context& source_val,
                             MergeStrategy strategy);
};
```

**合并策略说明**：

| 策略 | 对标量的行为 | 对数组的行为 |
|------|-------------|-------------|
| `error_on_conflict` | 冲突时抛出异常 | 冲突时抛出异常 |
| `last_write_wins` | 直接覆盖 | 直接替换（v3.9+ 规范） |
| `deep_merge` | 递归合并对象 | 直接替换（非拼接，v3.9+ 规范） |
| `array_concat` | 直接覆盖 | 追加到末尾 |
| `array_merge_unique` | 直接覆盖 | 追加去重 |

---

### 2.8 `BudgetController`（预算控制器）

**头文件**：`src/modules/budget/budget_controller.h`

```cpp
class BudgetController {
public:
    explicit BudgetController(std::optional<ExecutionBudget> initial_budget = std::nullopt);

    bool try_consume_node();          // 返回 false 表示超出预算
    bool try_consume_llm_call();
    bool try_consume_subgraph_depth();
    bool exceeded() const;

    void set_termination_target(const NodePath& target);
    std::optional<NodePath> get_termination_target() const;

    const std::optional<ExecutionBudget>& get_budget() const;
    void set_budget(std::optional<ExecutionBudget> budget);

private:
    std::optional<ExecutionBudget> budget_opt_;
    NodePath termination_target_ = "/__system__/budget_exceeded";
};
```

---

### 2.9 `TraceExporter`（执行追踪）

**头文件**：`src/modules/trace/trace_exporter.h`

```cpp
struct TraceRecord {
    std::string trace_id;
    NodePath node_path;
    std::string type;             // NodeType 的字符串表示
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    std::string status;           // "success" | "failed" | "skipped"
    std::optional<std::string> error_code;
    nlohmann::json context_delta; // 执行前后上下文变化（简化 diff）
    std::optional<NodePath> ctx_snapshot_key; // 关联的快照键（v3.1）
    nlohmann::json budget_snapshot;           // 执行时的预算状态快照
    nlohmann::json metadata;                  // 节点原始 metadata
    std::optional<std::string> llm_intent;   // 从注释解析的 LLM 意图
    std::string mode;                         // "dev" 或 "prod"
};

class TraceExporter {
public:
    void on_node_start(const NodePath& path, NodeType type,
                       const nlohmann::json& initial_context,
                       const std::optional<ExecutionBudget>& budget);

    void on_node_end(const NodePath& path,
                     const std::string& status,
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

### 2.10 `ResourceManager`（资源管理器）

**头文件**：`src/modules/scheduler/resource_manager.h`

```cpp
class ResourceManager {
public:
    void register_resource(const Resource& resource);
    bool has_resource(const NodePath& path) const;
    const Resource* get_resource(const NodePath& path) const;
    nlohmann::json get_resources_context() const;  // 将所有资源序列化为 JSON

private:
    std::unordered_map<NodePath, Resource> resources_;
};
```

**注意**：`ResourceNode` 在执行时通过 `NodeExecutor::execute_resource` 将资源注册到 `ResourceManager`；`TopoScheduler` 在启动时调用 `register_resources` 预注册图中声明的所有资源节点。

---

### 2.11 `ToolRegistry`（工具注册表）

**头文件**：`src/common/tools/registry.h`

```cpp
class ToolRegistry {
public:
    ToolRegistry();

    // 普通工具注册（模板化，支持任意 JSON→JSON 可调用对象）
    template<typename Func>
    void register_tool(std::string name, Func&& func);

    bool has_tool(const std::string& name) const;
    nlohmann::json call_tool(const std::string& name,
                             const std::unordered_map<std::string, std::string>& args);
    std::vector<std::string> list_tools() const;

    // LLM 工具注册与调用
    void register_llm_tool(std::string name,
                           std::unique_ptr<ILLMTool> tool,
                           const LLMParams& default_params = {});
    bool is_llm_tool(const std::string& name) const;
    const LLMParams& get_llm_params(const std::string& name) const;
    nlohmann::json call_llm_tool(const std::string& name,
                                 const std::string& prompt,
                                 const LLMParams& params = {});

private:
    std::unordered_map<
        std::string,
        std::function<nlohmann::json(const std::unordered_map<std::string, std::string>&)>
    > tools_;

    struct LLMToolEntry {
        std::unique_ptr<ILLMTool> tool;
        LLMParams default_params;
    };
    std::unordered_map<std::string, LLMToolEntry> llm_tools_;

    void register_default_tools();
};
```

---

### 2.12 `LlamaAdapter` 与 LLM 接口层

#### ILLMTool 接口（`src/common/llm/llm_tool.h`）
```cpp
struct LLMParams {
    float temperature = 0.7f;
    int max_tokens = 512;
    float top_p = 0.95f;
    int n_ctx = 2048;
    int n_threads = 4;
    std::string model;
};

struct LLMResult {
    bool success = false;
    std::string text;
    std::string error;
    int tokens_generated = 0;
};

class ILLMTool {
public:
    virtual ~ILLMTool() = default;
    virtual LLMResult generate(const std::string& prompt,
                               const LLMParams& params = {}) = 0;
    virtual bool is_available() const = 0;
    virtual std::string name() const = 0;
};
```

#### LlamaAdapter（`src/common/llm/llama_adapter.h`）

直接封装 llama.cpp C++ API，以 RAII 方式管理模型资源：

```cpp
class LlamaAdapter {
public:
    struct Config {
        std::string model_path;
        int n_ctx      = 2048;
        int n_threads  = 4;
        float temperature = 0.7f;
        float min_p    = 0.05f;
        int n_predict  = 512;
    };

    explicit LlamaAdapter(const Config& config);
    ~LlamaAdapter();

    std::string generate(const std::string& prompt);
    bool is_loaded() const;

private:
    Config config_;
    std::unique_ptr<llama_model,   decltype(&llama_model_free)> model_;
    std::unique_ptr<llama_context, decltype(&llama_free)>       ctx_;
    std::unique_ptr<llama_sampler, decltype(&llama_sampler_free)> sampler_;

    std::vector<llama_token> tokenize(const std::string& text, bool add_bos);
    std::string detokenize(llama_token token);
};
```

#### LlamaTool（`src/common/llm/llama_tool.h`）

实现 `ILLMTool` 接口，将 `LlamaAdapter` 适配为可注册到 `ToolRegistry` 的 LLM 工具：

```cpp
class LlamaTool : public ILLMTool {
public:
    explicit LlamaTool(const LlamaAdapter::Config& config);
    LLMResult generate(const std::string& prompt,
                       const LLMParams& params = {}) override;
    bool is_available() const override;
    std::string name() const override;
};
```

**集成方式**：
```cpp
// 示例：将 llama.cpp 模型注册为 LLM 工具
auto engine = DSLEngine::from_markdown(dsl_content);
auto llama_tool = std::make_unique<LlamaTool>(LlamaAdapter::Config{
    .model_path = "/path/to/model.gguf",
    .n_ctx = 4096,
    .temperature = 0.8f
});
engine->register_llm_tool("llama-7b", std::move(llama_tool));
```

---

### 2.13 `StandardLibraryLoader`（标准库加载器）

**头文件**：`src/modules/library/library_loader.h`

```cpp
struct LibraryEntry {
    std::string path;
    std::optional<std::string> signature;
    std::optional<nlohmann::json> output_schema;
    std::vector<std::string> permissions;
    bool is_subgraph = false;
};

class StandardLibraryLoader {
public:
    static StandardLibraryLoader& instance();   // 单例访问

    const std::vector<LibraryEntry>& get_available_libraries() const;
    void load_from_directory(const std::string& lib_dir);
    void load_builtin_libraries();

private:
    StandardLibraryLoader() = default;
    std::vector<LibraryEntry> libraries_;
    MarkdownParser parser_;
};
```

**注意**：`StandardLibraryLoader` 是当前代码中唯一保留的单例，计划在后续版本中重构为依赖注入模式。

---

## 三、与 C++ 推理内核的集成

执行器通过 `LlamaAdapter` / `ILLMTool` 双层抽象与推理内核交互：

1. **直接使用 LlamaAdapter**：`DSLEngine` 可持有 `std::unique_ptr<LlamaAdapter>`，由 `TopoScheduler` → `ExecutionSession` → `NodeExecutor` 层层传递（仅传指针）。适用于需要底层控制的场景。

2. **通过 ILLMTool 注册**：实现 `ILLMTool` 接口后，通过 `DSLEngine::register_llm_tool` 注册。`DSLNode`（`DSL_CALL` 节点）在执行时通过 `llm_tool_name` 字段选择对应的工具。适用于多模型切换、mock 测试等场景。

3. **内置 LlamaTool**：`LlamaTool` 已实现 `ILLMTool` 接口，内部持有 `LlamaAdapter` 实例，是最常见的集成方式。

```
DSLNode.llm_tool_name = "llama-7b"
    └─▶ ToolRegistry::call_llm_tool("llama-7b", prompt, params)
            └─▶ LlamaTool::generate(prompt, params)
                    └─▶ LlamaAdapter::generate(prompt)
                            └─▶ llama.cpp API
```

## 四、v1.0 实现边界（已实现功能）

### 4.1 已实现
- 完整支持核心节点类型：`assign`、`tool_call`、`dsl_call`（`DSLNode`）、`generate_subgraph`、`assert`、`fork/join`。
- `LLMCallNode`（旧 `llm_call`）向后兼容保留，但标记为 `[[deprecated]]`。
- 基于拓扑排序（入度队列）的 DAG 调度。
- Fork/Join **顺序模拟**（非真正并发）。
- `ContextEngine`：五种合并策略 + FIFO 快照管理。
- `BudgetController`：节点数、LLM 调用数、子图深度三维度预算控制。
- `TraceExporter`：完整的执行 Trace，含上下文 diff 和预算快照。
- `MarkdownParser`：YAML 代码块解析，支持多子图、签名、output_schema。
- `ToolRegistry`：普通工具 + LLM 工具（`ILLMTool`）双通道注册。
- `LlamaAdapter` / `LlamaTool`：基于 llama.cpp 的本地推理集成。
- `ResourceManager`：资源注册与查询。
- `StandardLibraryLoader`：从目录加载标准库子图。

### 4.2 暂不实现（v1.x 迭代计划）
- 真正的**并发执行**（`fork/join` 当前为顺序模拟）。
- **对话协议**（`/lib/conversation/**`）的完整状态管理。
- **世界模型/环境感知**原语。
- `StandardLibraryLoader` 重构为依赖注入（当前为单例）。
- `archive_to("/lib/...")` 的完整签名强制校验流程。
- Context TTL（`ttl_seconds`）自动过期机制。

## 五、验证用例映射

| 验证目标 | 执行器模块 | 测试文件 | 测试方法 |
|----------|------------|----------|----------|
| Markdown DSL 解析 | `MarkdownParser` | `test_parser.cpp` | 解析含多子图的 Markdown，验证节点数与类型 |
| 拓扑排序调度 | `TopoScheduler` | `test_scheduler.cpp` | 线性流程、跨图调用、soft termination |
| Fork/Join 模拟 | `TopoScheduler` | `test_scheduler.cpp` | 两分支 Fork → Join，验证上下文合并 |
| `assign` 节点 Inja 渲染 | `NodeExecutor` | `test_executor.cpp` | 模板变量替换、条件渲染 |
| `tool_call` 节点 | `NodeExecutor` | `test_executor.cpp` | 注册工具 → 调用 → 检查 output_keys |
| `dsl_call` 节点（DSLNode） | `NodeExecutor` | `test_executor.cpp` | 注册 LLM 工具 → DSLNode 调用 |
| LLM 工具注册与调用 | `ToolRegistry` | `test_tool_registry.cpp` | register_llm_tool → call_llm_tool |
| 动态 DSL 注入 | `DSLEngine` | `test_engine.cpp` | continue_with_generated_dsl → append → run |
| 无 LLM 执行 | `DSLEngine` | `test_no_llm.cpp` | 纯 assign/tool_call 节点，无 LlamaAdapter |
| 路径解析校验 | `parser_utils` | `test_path_resolution.cpp` | 有效/无效路径格式验证 |
| 上下文合并策略 | `ContextEngine` | `test_scheduler.cpp` | error_on_conflict、last_write_wins、deep_merge |
| 预算超限 | `BudgetController` | `test_scheduler.cpp` | max_nodes=2 时执行 3 节点，验证提前终止 |
| 标准库加载 | `StandardLibraryLoader` | `test_library_loader.cpp` | 从目录加载 /lib/** 子图 |
