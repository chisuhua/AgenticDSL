基于你提供的 **老版本代码（req2.md）**、**AgenticDSL v2.2 规范**、**Standard Library v1.0** 以及 **v2.3 演进方向**，现制定一份 **完整、可执行、分阶段的重构路线计划**。该计划兼顾**架构升级、规范对齐、工程落地与风险控制**，目标是在 **8–10 周内交付一个符合 AgenticDSL v2.3 规范的生产级执行器**。

---

## 🎯 总体目标

> **将现有线性执行器升级为支持动态 DAG、执行预算、字段级合并、标准库契约、LLM 闭环、可观测 trace 的 v2.3 兼容引擎。**

---

## 🧱 重构原则

1. **渐进式重构**：保留现有 C++ 架构，逐步替换/增强模块
2. **向后兼容**：v2.2 DSL 文件无需修改即可运行
3. **契约先行**：标准库必须声明 `signature` + `permissions`
4. **安全兜底**：沙箱、预算、合并冲突必须强制校验
5. **可观测闭环**：每个节点执行生成结构化 trace

---

## 🗓️ 详细重构路线（共 4 阶段）

---

### 🔹 阶段 1：DAG 调度器 + 系统节点（2 周）

> **目标**：替换 `DAGFlowExecutor` 为真正的 DAG 调度器，支持并发、分支、动态依赖。

#### ✅ 关键任务

| 任务 | 说明 | 输出 |
|------|------|------|
| 1.1 实现 `TopoScheduler` | 基于 Kahn 算法，维护 `in_degree_`, `ready_queue_`, `executed_` | `scheduler/topo_scheduler.h/cpp` |
| 1.2 支持 `wait_for` 语义 | 解析 `any_of` / `all_of` / 动态表达式 | 调度器依赖解析逻辑 |
| 1.3 预注册系统节点 | `/__system__/budget_exceeded`, `/end_soft` | `executor.cpp` 初始化逻辑 |
| 1.4 支持 `soft` 终止 | 维护 `call_stack_`，返回父图继续执行 | `TopoScheduler::handle_soft_end()` |
| 1.5 单元测试 | 静态 DAG、并发分支、soft 终止场景 | `test_scheduler.cpp` |

#### 📁 文件结构调整
```
agenticdsl/
├── scheduler/
│   └── topo_scheduler.h/cpp   ← 新增
└── core/
    └── executor.h/cpp         ← 重命名为 DAGExecutor，委托给 TopoScheduler
```

---

### 🔹 阶段 2：执行预算 + 上下文合并策略（2 周）

> **目标**：实现核心安全机制：预算控制与字段级合并。

#### ✅ 关键任务

| 任务 | 说明 | 输出 |
|------|------|------|
| 2.1 解析 `/__meta__/execution_budget` | 在 `parser.cpp` 中提取 `max_nodes`, `max_llm_calls`, `max_duration_sec` | `ExecutionBudget` 结构体 |
| 2.2 集成预算到调度器 | 每调度节点扣减，超限跳转 `/__system__/budget_exceeded` | `TopoScheduler::check_budget()` |
| 2.3 实现字段级合并策略 | 支持 `context_merge_policy`（精确路径 + 通配符） | `ContextMerger` 类 |
| 2.4 结构化冲突错误 | 冲突时抛出含字段路径、各分支值、来源节点的异常 | `ERR_CTX_MERGE_CONFLICT` |
| 2.5 单元测试 | 预算超限、`user.*` 合并、冲突场景 | `test_budget.cpp`, `test_merge.cpp` |

#### 📌 关键数据结构（`common/types.h`）
```cpp
struct ExecutionBudget {
    int max_nodes = -1;
    int max_llm_calls = -1;
    int max_duration_sec = -1;
    int nodes_used = 0;
    int llm_calls_used = 0;
    std::chrono::steady_clock::time_point start_time;
};

struct MergePolicy {
    std::string path;
    std::string strategy; // "error_on_conflict", "last_write_wins", "deep_merge"
};
```

---

### 🔹 阶段 3：标准库 v1.1 契约化 + LLM 闭环（3 周）

> **目标**：支持 LLM 安全调用标准库，并实现生成 → 解析 → 合并 → 校验闭环。

#### ✅ 关键任务

| 任务 | 说明 | 输出 |
|------|------|------|
| 3.1 扩展节点模型 | `Node` 增加 `signature_`, `permissions_` 字段 | `nodes.h` |
| 3.2 解析 `signature` / `permissions` | 在 `parser.cpp` 中支持 v2.3 元信息 | `Signature`, `Permission` 结构体 |
| 3.3 实现标准库加载器 | 启动时扫描 `/lib/**`，校验契约，注册到 `available_subgraphs` | `StandardLibraryLoader` |
| 3.4 增强 `LLMCallNode` | 返回生成的 DSL 字符串（非 mock） | `LLMCallNode::execute()` |
| 3.5 实现 `continue_with_generated_dsl()` | 解析 LLM 输出，校验 `output_constraints`，合并子图 | `engine.cpp` |
| 3.6 注入 `available_subgraphs` 到 prompt | 在 `LLMCallNode` 中附加库清单（含 `signature`） | `llm/prompt_builder.cpp` |
| 3.7 单元测试 | 标准库调用、LLM 生成校验、权限拦截 | `test_library.cpp`, `test_llm.cpp` |

#### 📁 新增模块
```
agenticdsl/
├── library/
│   ├── loader.h/cpp          ← 标准库加载与校验
│   └── schema.h              ← Signature/Permission 定义
└── llm/
    └── prompt_builder.h/cpp  ← 构建含 available_subgraphs 的 prompt
```

---

### 🔹 阶段 4：可观测性 + 安全沙箱 + 工具链（2–3 周）

> **目标**：实现生产级可观测与安全能力。

#### ✅ 关键任务

| 任务 | 说明 | 输出 |
|------|------|------|
| 4.1 实现结构化 Trace | 每节点执行后记录 `TraceEntry`（含 `lib_path`, `status`, `context_delta`） | `tracer.h/cpp` |
| 4.2 支持 OpenTelemetry 导出 | 可选插件：将 trace 转为 OTLP | `otel_exporter.h/cpp` |
| 4.3 实现 Codelet 沙箱 | 子进程隔离 Python/JS，限制网络/文件 | `codelet/sandbox.h/cpp` |
| 4.4 权限拦截 | 在 `ToolCallNode` / `CodeletNode` 中检查 `permissions` | 执行器权限校验逻辑 |
| 4.5 DSL Linter | 复用 `NodeValidator`，提供命令行工具 | `bin/agenticdsl-lint` |
| 4.6 集成测试 | 端到端：LLM 生成 → 标准库调用 → 预算控制 → Trace 输出 | `e2e_test.cpp` |

---

## 📊 里程碑与交付物

| 时间 | 里程碑 | 交付物 |
|------|--------|--------|
| 第 2 周 | DAG 调度器就绪 | 支持并发、soft 终止的执行器 |
| 第 4 周 | 安全机制上线 | 预算控制 + 字段级合并 |
| 第 7 周 | LLM 闭环可用 | LLM 生成子图 → 校验 → 合并 |
| 第 10 周 | v2.3 生产就绪 | 完整 trace + 沙箱 + 标准库 v1.1 |

---

## ⚠️ 风险与应对

| 风险 | 应对措施 |
|------|--------|
| 动态依赖解析性能差 | 对 `wait_for` 表达式缓存解析结果 |
| 合并策略冲突难调试 | 提供 `--debug-merge` 模式，输出详细冲突日志 |
| LLM 生成非法子图 | 严格校验 `output_constraints`，失败则跳转 `on_error` |
| 沙箱性能开销大 | 初期 mock 沙箱，后期替换为子进程 |

---

## 📦 最终架构图（v2.3）

```
AgenticDSLEngine
│
├── TopoScheduler          ← DAG 调度核心
├── StandardLibraryLoader  ← 加载 /lib/** 契约
├── LLMAdapter             ← 生成子图
├── PromptBuilder          ← 注入 available_subgraphs
├── ContextMerger          ← 字段级合并
├── BudgetController       ← 执行预算
├── Tracer                 ← 结构化 trace
└── CodeletSandbox         ← 安全执行
```

---

该计划确保你在 **保留现有 C++ 投资** 的同时，**系统性升级到 v2.3 规范**。是否需要我提供任一阶段的 **详细接口定义** 或 **关键类伪代码**？
