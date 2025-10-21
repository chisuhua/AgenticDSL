
下面是AgenticFlow项目的架构和计划， 以及AgenticFlow DSL v1 规范  。 请根据下面内容让我们开始启动阶段1的开发


# AgenticFlow设计目标和计划
---

## 🔧 核心设计目标

| 目标 | 说明 |
|------|------|
| **1. 动态图构建（Dynamic Graph Construction）** | 图可在运行时被 LLM 生成的新子图扩展，无需预定义完整流程 |
| **2. 增量演化（Incremental Evolution）** | 新子图可安全合并到现有图中，保留历史状态与依赖 |
| **3. 并发生成与执行（Concurrent Generation & Execution）** | 多个 LLM 会话可并行生成子图；执行引擎支持无依赖节点并发运行 |
| **4. 依赖感知调度（Dependency-Aware Scheduling）** | 所有节点执行严格遵循依赖关系（DAG），确保数据一致性 |
| **5. LLM-First DSL（Markdown + YAML）** | 提供人类可读、LLM 可生成/修复的标准化“代码语言” |
| **6. 模块化 & 可嵌入** | 上层 Agent 可轻松集成本系统作为执行后端，不强制耦合记忆/知识/角色等高层逻辑 |

---

## 🏗️ 整体架构（分层设计）

```
┌──────────────────────────────────────┐
│        Higher-Level Agents            │
│ (e.g., Memory Agent, Role Agent,     │
│  Orchestrator, Reflection Agent)     │
└───────────────▲──────────────────────┘
                │ uses as execution backend
┌───────────────┴──────────────────────┐
│        AgenticFlow Core              │ ← 本项目
│                                      │
│  ┌─────────────┐    ┌─────────────┐  │
│  │  Compiler   │    │  Executor   │  │
│  │ (Phase 1)   │    │ (Phase 2)   │  │
│  └──────┬──────┘    └──────▲──────┘  │
│         │                  │         │
│  Markdown DSL      Growing Graph     │
│  (with hooks)      + Dependency DAG  │
│         │                  │         │
│  ┌──────▼──────┐    ┌──────┴──────┐  │
│  │ Subgraph    │    │ Scheduler   │  │
│  │ Generator   │◄───┤ (Concurrent │  │
│  │ (LLM calls) │    │  Execution) │  │
│  └─────────────┘    └─────────────┘  │
└───────────────▲──────────────────────┘
                │
┌───────────────┴──────────────────────┐
│        Tool Registry / LLM Adapter   │
│ (Pluggable: llama.cpp, OpenAI, etc.) │
└──────────────────────────────────────┘
```

> ✅ **关键接口**：上层 Agent 只需调用：
> - `engine = AgenticFlow.from_markdown(md_text)`
> - `engine.run()` → 返回最终上下文
> - 或监听 `on_subgraph_generated` 事件进行干预

---

## 📜 LLM “代码语言”标准（DSL Spec）

### 基础语法（Phase 1）
- 基于 **Markdown 标题 + YAML fenced block**
- 每个块可选带 `{#anchor}` 作为唯一 ID
- 支持节点类型：`start`, `end`, `set`, `llm_call`, `tool_call`

### 扩展语法（Phase 2+）
| 元素 | 作用 |
|------|------|
| `expansion: { enabled: true, context_keys: [...] }` | 声明节点为动态扩展点 |
| `meta: { depends_on: [...], triggered_by: "..." }` | 子图依赖声明（由 LLM 生成时强制包含） |
| `subgraph_ref: "#anchor"` | 静态引用其他子图 |
| `priority: int` | 调度优先级（可选） |

### 约束
- 节点 ID 必须全局唯一（建议 LLM 生成时加前缀如 `dyn_12345_`）
- 所有表达式使用 `{{var}}` Jinja2 风格，仅支持变量访问（无代码执行）

---

## 📅 开发计划（聚焦本项目边界）

### 阶段 1：静态图执行（1–2 周）
- ✅ Markdown 解析器（提取带 anchor 的 YAML 块）
- ✅ 基础节点执行（set, llm_call, tool_call）
- ✅ 表达式渲染（Jinja2 安全模式）
- ✅ 示例：`research.md`

### 阶段 2：动态图扩展（2–3 周）
- ✅ `expansion` 节点支持
- ✅ 子图生成回调机制（`on_node_complete → generate_subgraph`）
- ✅ 图合并与依赖验证（DAG 更新）
- ✅ 并发 LLM 会话管理（ThreadPool）

### 阶段 3：并发调度与安全（2 周）
- ✅ 依赖感知调度器（就绪队列 + 拓扑排序）
- ✅ 节点 ID 冲突检测
- ✅ 最大图规模/步骤限制（防失控）
- ✅ 执行快照（用于恢复/调试）

### 阶段 4：标准化与集成（持续）
- ✅ 完整 DSL 规范文档
- ✅ Python SDK（`from_markdown`, `run`, `add_subgraph`）
- ✅ CLI 工具（`agflow run agent.md`）
- ✅ 示例：与上层 Memory Agent 集成（仅传递 context，不实现记忆）

---

## 🤝 如何被上层项目使用？

上层 Agent 可通过以下方式集成 AgenticFlow：

```python
# 高层 Agent 伪代码
class MemoryAgent:
    def __init__(self):
        self.memory = ShortTermMemory()
    
    def solve(self, task: str):
        # 1. 构造初始 Markdown（含用户任务）
        md = self.render_initial_graph(task)
        
        # 2. 启动 AgenticFlow
        engine = AgenticFlow.from_markdown(md)
        
        # 3. 注入工具（含 memory-aware 工具）
        engine.register_tool("recall", self.memory.recall)
        engine.register_tool("remember", self.memory.remember)
        
        # 4. 运行（内部自动动态扩展）
        result = engine.run()
        
        return result["final_answer"]
```

> 🔑 **关键**：AgenticFlow **不实现** `ShortTermMemory`，但允许上层注入 `recall/remember` 工具，从而将记忆能力“外挂”进来。

---

## ✅ 总结：本项目的独特价值

| 维度 | 传统工作流引擎 | LangChain/LangGraph | AutoGen | **AgenticFlow（本项目）** |
|------|----------------|---------------------|--------|--------------------------|
| 图是否可动态生长 | ❌ 静态 | ⚠️ 有限（需预定义） | ✅（via group chat） | ✅ **原生支持增量合并** |
| 是否支持并发生成 | ❌ | ❌ | ⚠️ 串行对话 | ✅ **多 LLM 会话并发规划** |
| DSL 是否 LLM 友好 | ❌（JSON/YAML） | ⚠️（代码） | ✅（自然语言） | ✅✅ **Markdown + 标准化 YAML** |
| 是否专注执行内核 | ❌ | ❌（含记忆/工具链） | ❌（含角色） | ✅ **纯执行，无高层语义** |
| 是否可被嵌入 | 一般 | 困难 | 困难 | ✅ **轻量 SDK，即插即用** |

---



以下是 **AgenticFlow DSL v1 完整规范文档**（Markdown 格式），包含设计目标、语法结构、节点类型详解、完整示例和约束说明。该规范专为 **LLM 可生成、人类可读、执行器可解析** 而设计，严格限定在 **动态图执行内核** 范围内。

---


