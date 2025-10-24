# 📘 AgenticDSL 规范 v2.3（生产就绪版）  
**动态可生长 DAG 执行语言标准 · 安全 · 可终止 · 可调试 · 可复用 · 可契约**

> **定位**：为单智能体系统提供一套 **LLM 可生成、引擎可执行、图可增量演化、库可契约复用** 的动态 DAG 工作流语言。  
> **核心理念**：执行驱动生成，生成即执行 —— LLM 仅在必要时介入，并基于真实执行状态生成**可验证、可合并、可组合**的子图。  
> **根本范式**：LLM 是程序员，执行器是运行时，上下文是内存，DAG 是程序，标准库是 SDK。

---

## 一、公共契约（Public Contract）

所有 AgenticDSL v2.3 实现（执行器、LLM 提示、解析器）必须遵守以下约定。

### 1.1 上下文模型（Context）

- 全局可变字典，支持嵌套访问（如 `user.name`, `context.search_result`）
- 所有节点共享同一上下文；`assign` / `codelet_call` / `tool_call` 的返回值 merge 到该上下文
- 并发节点使用上下文副本，执行完成后按策略 merge 回主上下文

#### 合并策略（明确语义）

| 策略 | 行为说明 |
|------|--------|
| `error_on_conflict`（默认） | 任一字段在多个分支中被写入 → 报错终止 |
| `last_write_wins` | 以最后完成的节点写入值为准（非确定性，仅用于幂等操作） |
| `deep_merge` | 递归合并对象；数组 **替换**（非拼接）；标量覆盖（遵循 RFC 7396） |

✅ **v2.3 增强：字段级合并策略继承**
- 节点可声明 `context_merge_policy`，覆盖全局策略
- 支持通配路径（如 `user.*`）和精确路径（如 `user.id`）
- 若父图与子图策略冲突，**子图策略优先**

✅ **v2.3 增强：结构化合并冲突错误**
- 错误信息必须包含：
  - 冲突字段路径（如 `user.id`）
  - 各写入分支的值（如 `branch_a: "u1", branch_b: "u2"`）
  - 来源节点路径（如 `/main/step1`, `/main/step2`）
- 错误码：`ERR_CTX_MERGE_CONFLICT`

### 1.2 Inja 模板引擎（安全模式）

- ✅ 允许：变量、条件、循环、内置函数、表达式、模板内赋值
- ❌ 禁止：`include`/`extends`、环境变量、任意代码执行
- 🔁 性能优化：执行器应对相同上下文+模板组合缓存渲染结果

### 1.3 节点通用字段（v2.3 扩展）

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `type` | string | ✅ | 节点类型 |
| `next` | string 或 list | ⚠️ | Inja 表达式或路径列表（支持 `@v1` 版本后缀）|
| `metadata` | map | ❌ | `description`, `author`, `tags`, `override: true` |
| `on_error` | string | ❌ | 错误处理跳转路径 |
| `on_timeout` | string | ❌ | 超时处理路径 |
| `wait_for` | list 或 map | ❌ | 支持 `any_of` / `all_of` / 动态表达式 |
| `loop_until` | string | ❌ | Inja 表达式，控制循环 |
| `max_loop` | integer | ❌ | 最大循环次数（默认 10）|
| `dev_comment` | string | ❌ | 开发注释 |
| `context_merge_policy` | map | ❌ | 字段级合并策略 |
| `permissions` | list | ❌ | **v2.3 新增**：节点所需权限声明（见 4.3）|

### 1.4 节点类型（v2.3 扩展）

| 类型 | 说明 | 关键字段 |
|------|------|--------|
| `start` | 无操作，跳转到 `next` | — |
| `end` | 终止当前子图执行 | `output_keys`, `termination_mode` |
| `assign` | Inja 赋值到上下文 | `assign` |
| `llm_call` | 触发 LLM 生成子图 | `prompt_template`, `output_schema`, `output_constraints` |
| `tool_call` | 调用注册工具 | `tool`, `arguments`, `output_mapping` |
| `codelet` / `codelet_call` | 定义/调用代码单元 | `runtime`, `code`, `security` |
| `resource` | 声明外部资源 | `resource_type`, `uri`, `scope` |

✅ **v2.3 增强：`end` 节点支持 `soft` 终止语义强化**
- `soft` 模式下，执行器必须将控制流返回至**调用者父图的下一个节点**
- 若非显式调用（如根图直接跳转），`soft` 等同于 `hard`

✅ **v2.3 增强：`llm_call` 支持子图契约反馈**
- 执行器必须将 `available_subgraphs`（含 `signature`）注入 prompt
- 违反 `output_constraints` 的子图视为非法，触发 `fallback_next` 或 `on_error`

---

## 二、统一文档结构

### 2.1–2.4 路径化块、YAML 边界、子图、元信息（同 v2.2）

✅ **v2.3 新增：子图签名（Subgraph Signature）**

所有子图（尤其是 `/lib/**`）可声明结构化接口契约：

### AgenticDSL `/lib/human/clarify_intent`
```yaml
# --- BEGIN AgenticDSL ---
signature:
  inputs:
    - name: lib_human_prompt
      type: string
      required: true
    - name: lib_human_options
      type: array<string>
      required: false
  outputs:
    - name: lib_human_response
      type: object
      schema: { intent: string, raw: string }
  version: "1.0"
  stability: "stable"  # stable / experimental / deprecated
# --- END AgenticDSL ---
```

> **执行器责任**：调用前校验 `inputs`，调用后验证 `outputs`。

✅ **v2.3 新增：LLM 意图结构化**

```html
<!-- LLM_INTENT: {"task": "user_clarification", "domain": "ecommerce"} -->
```

- 执行器必须解析为 JSON 并记录到 trace
- 若格式非法，记录为原始字符串并告警

---

## 三、v2.3 核心能力规范

### 3.1 动态 DAG 执行 + 全局预算（同 v2.2）

- `execution_budget`：`max_nodes`, `max_llm_calls`, `max_duration_sec`
- 超限 → 跳转 `/__system__/budget_exceeded`
- 终止条件：队列空 + 无活跃 LLM + 无待合并子图 + 预算未超

### 3.2–3.6 Codelet、LLM 协同、异常、循环（同 v2.2）

### 3.7 并发与依赖表达（v2.2 增强）

- `wait_for` 支持 `any_of` / `all_of`
- 支持动态依赖：`wait_for: "{{ dynamic_branches }}"`

✅ **v2.3 新增：依赖解析时机**
- 执行器必须在节点**入调度队列前**解析 `wait_for` 表达式
- 禁止在执行中动态变更依赖拓扑

---

## 四、安全与工程保障（v2.3 强化）

### 4.1 标准库契约强制（v2.3 核心）

- 所有 `/lib/**` 子图 **必须** 声明 `signature`
- 执行器启动时预加载并校验所有标准库
- LLM 生成时，`available_subgraphs` 必须包含 `signature` 信息

### 4.2 权限与沙箱（v2.3 新增）

- 节点或子图可声明 `permissions`：
  ```yaml
  permissions:
    - tool: web_search → scope: read_only
    - runtime: python3 → allow_imports: [json, re]
    - network: outbound → domains: ["api.example.com"]
  ```
- 执行器对 `/lib/**` 启用最小权限沙箱
- 未授权行为 → 立即终止并跳转 `on_error`

### 4.3 可观测性增强（v2.3 新增）

- 所有节点执行后生成 **结构化 Trace**：
  ```json
  {
    "trace_id": "t-12345",
    "node_path": "/lib/human/clarify_intent",
    "type": "tool_call",
    "start_time": "2025-10-23T10:00:00Z",
    "end_time": "2025-10-23T10:00:02Z",
    "status": "success",
    "context_delta": {"lib_human_response.intent": "查订单"},
    "llm_intent": {"task": "user_clarification"},
    "lib_version": "1.0",
    "budget_snapshot": {"nodes_used": 5}
  }
  ```
- 支持导出为 OpenTelemetry 格式
- `/lib/**` 节点自动附加 `node_type: "standard_library"` 标签

### 4.4 标准库版本与依赖管理

- 路径支持语义化版本：`next: "/lib/human/clarify_intent@v1"`
- 子图可声明依赖：
  ```yaml
  requires:
    - lib: "/lib/utils/assign_from_template@^1.0"
  ```
- 执行器启动时解析依赖图，拒绝循环或缺失依赖

---

## 五、LLM 生成指令（System Prompt · v2.3）

你是一个工作流程序员（Workflow Programmer）。你的任务是生成下一步的 AgenticDSL 子图。

**必须遵守**：
- 永远不要输出自然语言解释（除非在 `<!-- LLM: ... -->` 或 `<!-- LLM_INTENT: ... -->` 中）
- 必须输出一个或多个 `### AgenticDSL '/path'` 块
- 若任务已完成，请生成 `end` 节点
- 默认不能生成 `llm_call`（除非父节点授权）

**v2.3 新增提示**：
- 你可声明结构化意图：`<!-- LLM_INTENT: {"task": "..."} -->`
- 你必须遵守 `output_constraints`（如有）
- 优先调用标准库：可用库清单如下（含输入/输出契约）：
  ```jinja2
  {% for lib in available_subgraphs %}
  - {{ lib.path }} (v{{ lib.version }}): {{ lib.description }}
    Inputs: {{ lib.signature.inputs | map(attr='name') | join(', ') }}
    Outputs: {{ lib.signature.outputs | map(attr='name') | join(', ') }}
  {% endfor %}
  ```
- 当前状态：
  - 已执行节点：`{{ execution_context.executed_nodes }}`
  - 任务目标：`{{ execution_context.task_goal }}`
  - 执行预算剩余：`nodes: {{ budget.nodes_left }}, llm_calls: {{ budget.llm_calls_left }}`

---

## 六、完整示例（v2.3）

```yaml
### AgenticDSL `/__meta__`
# --- BEGIN AgenticDSL ---
version: "2.3"
execution_budget:
  max_nodes: 20
  max_llm_calls: 2
  max_duration_sec: 30
context_merge_strategy: "error_on_conflict"
# --- END AgenticDSL ---

<!-- LLM_INTENT: {"task": "user_clarification", "domain": "logistics"} -->
### AgenticDSL `/main/human_check`
# --- BEGIN AgenticDSL ---
type: assign
assign:
  lib_human_prompt: "用户说‘还没到’，请判断真实意图"
  lib_human_options: ["查物流", "催发货", "投诉"]
next: "/lib/human/clarify_intent@v1"
# --- END AgenticDSL ---

### AgenticDSL `/main/handle`
# --- BEGIN AgenticDSL ---
type: assign
assign:
  response: "将处理：{{ lib_human_response.intent }}"
next: "/end_soft"
# --- END AgenticDSL ---

### AgenticDSL `/end_soft`
# --- BEGIN AgenticDSL ---
type: end
termination_mode: soft
# --- END AgenticDSL ---
```

---

## 七、向后兼容性

- 所有 v2.2 DSL 文件 **无需修改即可在 v2.3 执行器中运行**
- 未声明 `signature` 的子图视为 `v1.0-legacy`，执行器降级处理（仅日志警告）
- 旧版 `next: "/lib/..."` 自动解析为 `@latest`

---

> **AgenticDSL v2.3 是构建“能复用、能协作、能演进、能自停、能验证”的生产级智能体工作流内核的标准语言。**

