# AgenticDSL v3.1 规范  
**AI-Native Dynamic DAG Language for Action & Reasoning**  
**安全 · 可终止 · 可调试 · 可复用 · 可契约 · 可验证**

> **最后更新**：2025年10月28日  
> **规范版本**：v3.1（稳定草案）  
> **适用对象**：LLM 架构师、智能体引擎开发者、AI 工作流设计者  
> **许可协议**：CC BY-SA 4.0（欢迎社区共建）

---

## 一、核心理念与定位

### 1.1 定位
AgenticDSL 是一套 **AI-Native 的声明式动态 DAG 语言**，专为单智能体及未来多智能体系统设计，支持：
- **LLM 可生成**：大模型能输出结构化、可执行的子图
- **引擎可执行**：确定性调度、状态合并、预算控制
- **DAG 可动态生长**：运行时生成新子图，支持思维流与行动流
- **标准库可契约复用**：`/lib/**` 带签名，最小权限沙箱
- **推理可验证进化**：通过 `assert`、Trace、`archive_to` 实现闭环优化

### 1.2 根本范式
| 角色 | 职责 |
|------|------|
| **LLM** | 程序员：基于真实状态生成可验证子图 |
| **执行器** | 运行时：确定性调度、状态合并、预算控制 |
| **上下文** | 内存：结构可契约、合并可策略、冲突可诊断 |
| **DAG** | 程序：图可增量演化，支持行动流与思维流 |
| **标准库** | SDK：`/lib/**` 必须带 `signature`，最小权限沙箱 |

### 1.3 设计原则
- **确定性优先**：所有节点必须在有限时间内完成，禁止异步回调
- **契约驱动**：接口必须声明，调用必须验证
- **最小权限**：节点/子图需显式声明所需权限
- **可终止性**：全局预算控制，防止无限循环或生成
- **可观测性**：每个节点生成结构化 Trace，支持调试与训练

---

## 二、节点抽象层级（v3.1 核心增强）

AgenticDSL 节点分为三层，确保语义清晰与可演进性：

| 层级 | 说明 | 约束 |
|------|------|------|
| **1. 执行原语层（叶子节点）** | 规范内置、不可扩展的最小操作单元 | 禁止用户自定义新类型 |
| **2. 推理原语层（规范子图）** | 规范提供的稳定推理模式实现 | 路径：`/lib/reasoning/**`，版本稳定，由规范维护 |
| **3. 知识应用层（标准库子图）** | 用户/社区扩展的领域逻辑 | 路径：`/lib/workflow/**`, `/lib/knowledge/**`，需带 `signature` |

> ✅ **所有复杂逻辑必须通过子图组合实现，禁止在叶子节点中编码高层语义。**

---

## 三、术语表（v3.1 新增）

| 术语 | 定义 |
|------|------|
| **子图（Subgraph）** | 一个以 `### AgenticDSL '/path'` 开头的逻辑单元，可被其他节点调用 |
| **动态生长（Dynamic Growth）** | 通过 `generate_subgraph` 节点在运行时生成新子图并注册到 `/dynamic/**` |
| **契约（Contract）** | 由 `signature` 定义的输入/输出接口规范，用于调用前校验与调用后验证 |
| **软终止（Soft Termination）** | 子图执行结束时返回调用者上下文，而非终止整个 DAG |
| **核心标准库（Core SDK）** | v3.1 强制要求实现的 `/lib/**` 子图集合（见附录 C） |
| **执行原语层** | 内置叶子节点（如 `assign`, `assert`），不可扩展 |
| **推理原语层** | 规范维护的 `/lib/reasoning/**` 子图，实现通用推理模式 |

---

## 四、公共契约

### 4.1 上下文模型（Context）

- 全局可变字典，支持嵌套路径（如 `user.name`, `search_results[0].title`）
- 所有节点共享同一上下文；`assign` / `tool_call` / `codelet_call` 的返回值 **merge 到该上下文**
- 并发节点使用上下文副本，执行完成后按策略 merge 回主上下文

#### 合并策略（字段级、可继承）

| 策略 | 行为说明 |
|------|--------|
| `error_on_conflict`（默认） | 任一字段在多个分支中被写入 → 报错终止 |
| `last_write_wins` | 以最后完成的节点写入值为准（非确定性，仅用于幂等操作） |
| `deep_merge` | 递归合并对象；**数组替换（非拼接）**；标量覆盖（遵循 RFC 7396） |
| **`array_concat`**（v3.0 新增） | **数组拼接**（保留顺序，允许重复） |
| **`array_merge_unique`**（v3.0 新增） | 数组拼接 + 去重（基于 JSON 序列化值） |

✅ **字段级策略继承**  
- 节点可声明 `context_merge_policy`，覆盖全局策略  
- 支持通配路径（如 `results.*`）和精确路径（如 `results.items`）  
- 子图策略优先于父图

✅ **结构化合并冲突错误**  
错误信息必须包含：  
- 冲突字段路径（如 `user.id`）  
- 各写入分支的值（如 `branch_a: "u1", branch_b: "u2"`）  
- 来源节点路径（如 `/main/step1`, `/main/step2`）  
- 错误码：`ERR_CTX_MERGE_CONFLICT`

---

### 4.2 Inja 模板引擎（安全模式）

✅ 允许：变量、条件、循环、内置函数、表达式、模板内赋值  
❌ 禁止：`include`/`extends`、环境变量、任意代码执行  
🔁 性能优化：执行器应对相同上下文+模板组合缓存渲染结果

---

### 4.3 节点通用字段

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `type` | string | ✅ | 节点类型 |
| `next` | string 或 list | ⚠️ | Inja 表达式或路径列表（支持 `@v1` 版本后缀） |
| `metadata` | map | ❌ | `description`, `author`, `tags` |
| `on_error` | string | ❌ | 错误处理跳转路径 |
| `on_timeout` | string | ❌ | 超时处理路径 |
| `on_success` | string | ❌ | 成功后动作（如 `archive_to("/lib/solved/...")`） |
| `wait_for` | list 或 map | ❌ | 支持 `any_of` / `all_of` / 动态表达式 |
| `loop_until` | string | ❌ | Inja 表达式，控制循环 |
| `max_loop` | integer | ❌ | 最大循环次数（默认 10） |
| `context_merge_policy` | map | ❌ | 字段级合并策略 |
| `permissions` | list | ❌ | 节点所需权限声明（见 7.2） |
| `expected_output` | map | ❌ | 声明期望输出（用于验证/训练） |
| `curriculum_level` | string | ❌ | 课程难度标签（如 `beginner`） |

> ❌ **移除 `dev_comment`**：建议使用标准 Markdown 注释（如 `<!-- debug: ... -->`）

---

## 五、核心叶子节点定义（执行原语层）

### 5.1 `assign`
- **语义**：安全赋值到上下文（Inja 表达式）
- **关键字段**：`assign.expr`, `assign.path`（可选）
- **示例**：
  ```yaml
  type: assign
  assign:
    expr: "{{ $.a + $.b }}"
    path: "result.sum"
  ```

### 5.2 `tool_call`
- **语义**：调用注册工具（带权限检查）
- **关键字段**：`tool`, `arguments`, `output_mapping`
- **权限要求**：必须声明 `permissions`（如 `tool: web_search`）

### 5.3 `codelet_call`
- **语义**：执行沙箱代码（带安全策略）
- **关键字段**：`runtime`, `code`, `security`
- **权限要求**：必须声明 `permissions`（如 `runtime: python3`）

### 5.4 `assert`
- **语义**：验证条件，失败则跳转
- **关键字段**：`condition`（Inja 布尔表达式）, `on_failure`
- **示例**：
  ```yaml
  type: assert
  condition: "len($.roots) == 1"
  on_failure: "/self/repair"
  ```

### 5.5 `fork` / `join`
- **语义**：显式并行控制
- **关键字段**：
  - `fork.branches`: 路径列表
  - `join.wait_for`: 依赖列表, `merge_strategy`
- **依赖解析时机**：执行器必须在节点入调度队列前解析 `wait_for` 表达式  
- **禁止**：在执行中动态变更依赖拓扑

### 5.6 `end`
- **语义**：终止当前子图
- **关键字段**：
  - `termination_mode`: `hard`（默认）或 `soft`
  - `output_keys`: 仅合并指定字段到父上下文（`soft` 模式）
- **`soft` 语义**：  
  > 执行器维护调用栈。`soft end` 弹出栈顶，跳转至调用者的 `next` 节点。若栈空，则等同 `hard`。

### 5.7 `generate_subgraph`（v3.0 新增，原 `llm_call`）
- **语义**：**委托 LLM 生成一个或多个新的可执行子图（DAG 片段）**  
  > ⚠️ **不得用于调用已有子图或生成自然语言！**
- **关键字段**：
  - `prompt_template`
  - `output_constraints`（如 `must_include_signature`）
  - `signature_validation`: `strict`（默认）, `warn`, `ignore`
  - `on_signature_violation`: 签名验证失败跳转路径
- **执行器行为**：
  1. 注入 `available_subgraphs`（含 `signature`）到 prompt
  2. 解析 LLM 输出的 `### AgenticDSL '/dynamic/...'` 块
  3. 注册到 `/dynamic/**` 命名空间
  4. 若声明 `signature`，按策略校验

### 5.8 `start` / `resource`
- `start`：无操作，跳转到 `next`
- `resource`：**声明式依赖**（非执行节点），执行器在**调度前检查**资源可用性（凭据、网络、权限）

---

## 六、统一文档结构

### 6.1 路径化子图块（核心单元）

- 所有逻辑单元均为 **`### AgenticDSL '/path'` 块**
- `.agent.md` 文件是**多个子图块的物理打包格式**
- 路径命名空间：
  - `/lib/**`：**静态标准库**（必须带 `signature`）
    - `/lib/reasoning/**`：推理原语（规范维护）
    - `/lib/workflow/**`：行动流模块
    - `/lib/knowledge/**`：知识单元
    - `/lib/human/**`：人机协作模块
  - `/dynamic/**`：**运行时生成子图**
  - `/main/**`：主流程

### 6.2 子图签名（Subgraph Signature）

所有 `/lib/**` 子图 **必须** 声明结构化接口契约：

```yaml
signature:
  inputs:
    - name: expr
      type: string
      required: true
  outputs:
    - name: roots
      type: array
      schema:  # ✅ 强制 JSON Schema Draft 7+
        type: array
        items: { type: number }
        minItems: 1
  version: "1.0"
  stability: stable  # stable / experimental / deprecated
```

- **`signature.outputs`**：定义接口契约（调用前后校验）
- **`expected_output`**：定义单次任务期望值（用于 Trace 验证）

### 6.3 LLM 意图结构化

```html
<!-- LLM_INTENT: {"task": "user_clarification", "domain": "ecommerce"} -->
```

- 执行器必须解析为 JSON 并记录到 trace
- 若格式非法，记录为原始字符串并告警

---

## 七、安全与工程保障

### 7.1 标准库契约强制

- 所有 `/lib/**` 子图 **必须** 声明 `signature`
- 执行器启动时预加载并校验所有标准库
- LLM 生成时，`available_subgraphs` 必须包含 `signature` 信息

### 7.2 权限与沙箱

节点或子图可声明 `permissions`：

```yaml
permissions:
  - tool: web_search → scope: read_only
  - runtime: python3 → allow_imports: [json, re]
  - network: outbound → domains: ["api.example.com"]
  - generate_subgraph: { max_depth: 2 }
```

- 执行器对 `/lib/**` 启用**最小权限沙箱**
- 未授权行为 → 立即终止并跳转 `on_error`

### 7.3 可观测性（Trace Schema）

所有节点执行后生成结构化 Trace（OpenTelemetry 兼容）：

```json
{
  "trace_id": "t-12345",
  "node_path": "/lib/reasoning/assert_real_root",
  "type": "assert",
  "start_time": "2025-10-23T10:00:00Z",
  "end_time": "2025-10-23T10:00:02Z",
  "status": "failed",
  "error_code": "ERR_ASSERT_FAILED",
  "context_delta": { "is_valid": false },
  "expected_output": { "roots": ["-1"] },
  "output_match": false,
  "suggested_fix": "请将方程重写为标准形式 ax^2 + bx + c = 0",
  "llm_intent": { "task": "math_reasoning" },
  "lib_version": "1.0",
  "node_type": "standard_library",
  "mode": "dev",
  "budget_snapshot": { "nodes_used": 5, "subgraph_depth": 1 }
}
```

### 7.4 标准库版本与依赖管理

- 路径支持语义化版本：`next: "/lib/human/clarify_intent@v1"`
- 子图可声明依赖：
  ```yaml
  requires:
    - lib: "/lib/reasoning/verify_solution@^1.0"
  ```
- 执行器启动时解析依赖图，拒绝循环或缺失依赖

---

## 八、核心能力规范

### 8.1 动态 DAG 执行 + 全局预算

- `execution_budget`：`max_nodes`, `max_subgraph_depth`, `max_duration_sec`
- 超限 → 跳转 `/__system__/budget_exceeded`
- **终止条件**：队列空 + 无活跃生成 + 无待合并子图 + 预算未超

### 8.2 动态子图生成（`generate_subgraph` 核心机制）

- LLM **必须输出一个或多个 `### AgenticDSL '/dynamic/...'` 块**
- 执行器解析为子图对象，注册到 `/dynamic/**` 命名空间
- 若声明 `signature`，则按 `signature_validation` 策略校验
- 新子图可被后续节点通过 `next: "/dynamic/plan_123"` 调用

### 8.3 并发与依赖表达

- `wait_for` 支持 `any_of` / `all_of`
- 支持动态依赖：`wait_for: "{{ dynamic_branches }}"`
- ✅ **依赖解析时机**：执行器必须在节点入调度队列前解析 `wait_for` 表达式  
- ❌ **禁止**：在执行中动态变更依赖拓扑

### 8.4 自进化控制

- `on_success: archive_to("/lib/solved/{{ problem_type }}@v1")`  
  → 成功 DAG 自动存入图库
- `on_error` 可跳转至修复子图（如 `/self/repair`）
- `curriculum_level` 支持课程学习调度

### 8.5 开发模式支持（v3.1 新增）

- 在 `/__meta__` 中声明 `mode: dev | prod`
- **开发模式**（`dev`）：
  - 默认 `signature_validation: warn`
  - 允许 `last_write_wins`
  - Trace 包含详细上下文快照
- **生产模式**（`prod`，默认）：
  - 强制 `signature_validation: strict`
  - 禁用 `last_write_wins`
  - 最小权限沙箱强制启用

---

## 九、LLM 生成指令（System Prompt）

> 你是一个**推理与行动架构师**（Reasoning & Action Architect）。你的任务是生成**可执行、可验证的动态 DAG**，包含：
> - **行动流**：调用工具、与人协作
> - **思维流**：假设 → 计算 → 验证
>
> **必须遵守**：
> - 永远不要输出自然语言解释（除非在 `<!-- LLM: ... -->` 或 `<!-- LLM_INTENT: ... -->` 中）
> - **必须输出一个或多个 `### AgenticDSL '/path'` 块**
> - 若任务已完成，请生成 `end` 节点
> - 你可生成 `generate_subgraph` 节点，但总递归深度不得超过 {{ budget.subgraph_depth_left }}
>
> **新增提示**：
> - 你可声明结构化意图：`<!-- LLM_INTENT: {"task": "..."} -->`
> - 你必须遵守 `output_constraints`（如有）
> - 优先调用标准库：

```jinja2
可用库清单（含契约）：
{% for lib in available_subgraphs %}
- {{ lib.path }} (v{{ lib.version }}): {{ lib.description }}
  Inputs: {{ lib.signature.inputs | map(attr='name') | join(', ') }}
  Outputs: {{ lib.signature.outputs | map(attr='name') | join(', ') }}
{% endfor %}
```

**当前上下文**：
- 已执行节点：`{{ execution_context.executed_nodes }}`
- 任务目标：`{{ execution_context.task_goal }}`
- 执行预算剩余：`nodes: {{ budget.nodes_left }}, depth: {{ budget.subgraph_depth_left }}`
- （训练模式）期望输出：`{{ expected_output }}`

---

## 十、完整示例

```markdown
### AgenticDSL `/__meta__`
yaml
version: "3.1"
mode: dev  # ✅ 开发模式
execution_budget:
  max_nodes: 20
  max_subgraph_depth: 2
  max_duration_sec: 30
context_merge_strategy: "error_on_conflict"

### AgenticDSL `/main/solve_equation`
yaml
type: assign
assign:
  expr: "x^2 + 2x + 1 = 0"
next: "/lib/reasoning/solve_quadratic@v1"

### AgenticDSL `/main/verify`
yaml
type: assert
condition: "len($.roots) == 1 and $.roots[0] == -1"
expected_output:
  roots: [-1]
on_success: "archive_to('/lib/solved/quadratic@v1')"
on_failure: "/self/repair"

### AgenticDSL `/self/repair`
yaml
type: generate_subgraph
prompt_template: "方程 {{ $.expr }} 求解失败。请重写为标准形式并生成新DAG。"
signature_validation: warn
on_signature_violation: "/self/fallback"
next: "/dynamic/repair_123"

### AgenticDSL `/lib/human/approval`  # ✅ Core SDK 示例
yaml
signature:
  inputs:
    - name: request
      type: string
      required: true
  outputs:
    - name: approved
      type: boolean
      required: true
    - name: comment
      type: string
      schema: { type: string }
  version: "1.0"
  stability: stable
type: tool_call
tool: human_approval
arguments:
  message: "{{ $.request }}"
output_mapping:
  approved: "result.approved"
  comment: "result.comment"
```

---

## 附录 A：最佳实践与约定

### A1. 时间上下文约定（非强制）
- `$.now`: ISO8601 当前时间（由执行器注入）
- `$.time_anchor`: 任务参考时间点
- `$.timeline[]`: `{ts: "...", event: "...", source: "..."}`

### A2. 禁止行为清单
- 在 DAG 内实现异步回调
- 在叶子节点中编码高层推理逻辑
- 使用 `generate_subgraph` 调用已有子图
- 输出非 `### AgenticDSL` 块的 LLM 内容
- 在生产模式下使用 `last_write_wins` 合并策略

### A3. 推荐工具链
- **验证器**：校验 `.agent.md` 文件语法与契约
- **可视化器**：渲染 DAG 执行路径
- **训练器**：从 Trace 中提取 `(input, expected_output, actual_output)` 三元组
- **模拟器**：dry-run 模式测试 DAG 行为

---

## 附录 B：`expected_output` 与 `signature.outputs` 分工说明

| 机制 | 作用域 | 用途 | 校验时机 |
|------|--------|------|--------|
| `signature.outputs` | **子图接口** | 契约：调用者与被调用者约定 | 调用前（输入）、调用后（输出） |
| `expected_output` | **单次执行** | 验证：本次任务期望的具体值 | 执行后（Trace 记录，可选告警） |

---

## 附录 C：核心标准库（Core SDK）v3.1 必须实现清单

| 路径 | 用途 | 稳定性 |
|------|------|--------|
| `/lib/reasoning/assert` | 中间结论验证 | stable |
| `/lib/human/clarify_intent` | 请求用户澄清意图 | stable |
| `/lib/human/approval` | 人工审批节点 | stable |
| `/lib/workflow/parallel_map` | 基于 `fork` 的 map 封装 | experimental |
| `/lib/reasoning/solve_quadratic` | 二次方程求解示例 | experimental |

> 执行器必须预加载并校验以上子图。社区可扩展，但不得修改其 `signature`。

---

> **AgenticDSL v3.1 是迈向 AI 原生操作系统的坚实一步。**  
> 它不仅定义了“如何运行思维”，更通过 **三层抽象 + Core SDK + 开发模式 + JSON Schema 契约**，  
> 为构建**可靠、可协作、可进化的智能体生态**提供了工程基石。

---  
**© 2025 AgenticDSL Working Group. All rights reserved.**  
*本规范采用 CC BY-SA 4.0 许可，欢迎社区共建。*
