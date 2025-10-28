# AgenticDSL v3.0 规范草案  
**AI-Native Dynamic DAG Language for Action & Reasoning**  
**安全 · 可终止 · 可调试 · 可复用 · 可契约 · 可验证**

> **定位**：为单智能体及未来多智能体系统提供一套 **LLM 可生成、引擎可执行、DAG 可动态生长、标准库可契约复用、推理可验证进化** 的声明式动态 DAG 语言。  
> **根本范式**：  
> - **LLM 是程序员**（基于真实状态生成可验证子图）  
> - **执行器是运行时**（确定性调度、状态合并、预算控制）  
> - **上下文是内存**（结构可契约、合并可策略、冲突可诊断）  
> - **DAG 是程序**（图可增量演化，支持行动流与思维流）  
> - **标准库是 SDK**（`/lib/**` 必须带 `signature`，最小权限沙箱）  

---

## 一、公共契约（Public Contract）

### 1.1 上下文模型（Context）

- 全局可变字典，支持嵌套路径（如 `user.name`, `search_result.docs[0].title`）
- 所有节点共享同一上下文；`assign` / `tool_call` / `codelet_call` 的返回值 **merge 到该上下文**
- 并发节点使用上下文副本，执行完成后按策略 merge 回主上下文

#### 合并策略（字段级、可继承）

| 策略 | 行为说明 |
|------|--------|
| `error_on_conflict`（默认） | 任一字段在多个分支中被写入 → 报错终止 |
| `last_write_wins` | 以最后完成的节点写入值为准（非确定性，仅用于幂等操作） |
| `deep_merge` | 递归合并对象；**数组替换（非拼接）**；标量覆盖（遵循 RFC 7396） |

✅ **字段级策略继承**  
- 节点可声明 `context_merge_policy`，覆盖全局策略  
- 支持通配路径（如 `user.*`）和精确路径（如 `user.id`）  
- 子图策略优先于父图

✅ **结构化合并冲突错误**  
错误信息必须包含：  
- 冲突字段路径（如 `user.id`）  
- 各写入分支的值（如 `branch_a: "u1", branch_b: "u2"`）  
- 来源节点路径（如 `/main/step1`, `/main/step2`）  
- 错误码：`ERR_CTX_MERGE_CONFLICT`

---

### 1.2 Inja 模板引擎（安全模式）

✅ 允许：变量、条件、循环、内置函数、表达式、模板内赋值  
❌ 禁止：`include`/`extends`、环境变量、任意代码执行  
🔁 性能优化：执行器应对相同上下文+模板组合缓存渲染结果

---

### 1.3 节点通用字段

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `type` | string | ✅ | 节点类型 |
| `next` | string 或 list | ⚠️ | Inja 表达式或路径列表（支持 `@v1` 版本后缀） |
| `metadata` | map | ❌ | `description`, `author`, `tags`, `override: true` |
| `on_error` | string | ❌ | 错误处理跳转路径 |
| `on_timeout` | string | ❌ | 超时处理路径 |
| `on_success` | string | ❌ | **v3.0 新增**：成功后动作（如 `archive_to("/lib/solved/...")`） |
| `wait_for` | list 或 map | ❌ | 支持 `any_of` / `all_of` / 动态表达式 |
| `loop_until` | string | ❌ | Inja 表达式，控制循环 |
| `max_loop` | integer | ❌ | 最大循环次数（默认 10） |
| `dev_comment` | string | ❌ | 开发注释 |
| `context_merge_policy` | map | ❌ | 字段级合并策略 |
| `permissions` | list | ❌ | 节点所需权限声明（见 4.2） |
| `expected_output` | map | ❌ | **v3.0 新增**：声明期望输出（用于验证/训练） |
| `curriculum_level` | string | ❌ | **v3.0 新增**：课程难度标签（如 `beginner`） |

---

### 1.4 节点类型

| 类型 | 说明 | 关键字段 |
|------|------|--------|
| `start` | 无操作，跳转到 `next` | — |
| `end` | 终止当前子图执行 | `output_keys`, `termination_mode` |
| `assign` | Inja 赋值到上下文 | `assign` |
| `llm_call` | **核心：触发 LLM 生成新子图** | `prompt_template`, `output_schema`, `output_constraints` |
| `tool_call` | 调用注册工具 | `tool`, `arguments`, `output_mapping` |
| `codelet` / `codelet_call` | 定义/调用代码单元 | `runtime`, `code`, `security` |
| `resource` | 声明外部资源 | `resource_type`, `uri`, `scope` |
| **`assert`** | **v3.0 新增：验证中间结论** | `condition`, `on_failure` |
| **`fork`** | **v3.0 新增：显式启动并行分支** | `branches: [path1, path2]` |
| **`join`** | **v3.0 新增：汇聚并行结果** | `wait_for`, `merge_strategy` |
| **`reasoning_step`** | **v3.0 新增（可选）：标记推理阶段** | `step_type: hypothesis/computation/verification` |

✅ **`end` 节点支持 `soft` 终止语义强化**  
- `soft` 模式下，执行器必须将控制流返回至**调用者父图的下一个节点**  
- 若非显式调用（如根图直接跳转），`soft` 等同于 `hard`

✅ **`llm_call` 支持子图契约反馈**  
- 执行器必须将 `available_subgraphs`（含 `signature`）注入 prompt  
- 违反 `output_constraints` 的子图视为非法，触发 `fallback_next` 或 `on_error`

---

## 二、统一文档结构

### 2.1 路径化子图块（核心单元）

- 所有逻辑单元均为 **`### AgenticDSL '/path'` 块**
- `.agent.md` 文件是**多个子图块的物理打包格式**
- 路径命名空间：
  - `/lib/**`：**静态标准库**（必须带 `signature`）
    - `/lib/workflow/**`：行动流模块（如客服、审批）
    - **`/lib/reasoning/**`：v3.0 新增，推理模式库（如验证、假设）**
  - `/dynamic/**`：**运行时生成子图**
  - `/main/**`：主流程

### 2.2 子图签名（Subgraph Signature）

所有 `/lib/**` 子图 **必须** 声明结构化接口契约：

```yaml
signature:
  inputs:
    - name: lib_human_prompt
      type: string
      required: true
  outputs:
    - name: lib_human_response
      type: object
      schema: { intent: string, raw: string }
  version: "1.0"
  stability: stable  # stable / experimental / deprecated
```

- 执行器责任：调用前校验 `inputs`，调用后验证 `outputs`
- LLM 生成时，`available_subgraphs` 必须包含 `signature` 信息

### 2.3 LLM 意图结构化

```html
<!-- LLM_INTENT: {"task": "user_clarification", "domain": "ecommerce"} -->
```

- 执行器必须解析为 JSON 并记录到 trace
- 若格式非法，记录为原始字符串并告警

---

## 三、核心能力规范

### 3.1 动态 DAG 执行 + 全局预算

- `execution_budget`：`max_nodes`, `max_llm_calls`, `max_duration_sec`
- 超限 → 跳转 `/__system__/budget_exceeded`
- **终止条件**：队列空 + 无活跃 LLM + 无待合并子图 + 预算未超

### 3.2 动态子图生成（`llm_call` 核心机制）

- LLM **必须输出一个或多个 `### AgenticDSL '/path'` 块**
- 执行器解析为子图对象，注册到 `/dynamic/**` 命名空间
- 若声明 `signature`，则校验输入/输出契约
- 新子图可被后续节点通过 `next: "/dynamic/plan_123"` 调用

### 3.3 并发与依赖表达

- `wait_for` 支持 `any_of` / `all_of`
- 支持动态依赖：`wait_for: "{{ dynamic_branches }}"`
- ✅ **依赖解析时机**：执行器必须在节点入调度队列前解析 `wait_for` 表达式  
- ❌ **禁止**：在执行中动态变更依赖拓扑

### 3.4 自进化控制（v3.0 新增）

- `on_success: archive_to("/lib/solved/{{ problem_type }}@v1")`  
  → 成功 DAG 自动存入图库
- `on_error` 可跳转至修复子图（如 `/self/repair`）
- `curriculum_level` 支持课程学习调度

---

## 四、安全与工程保障

### 4.1 标准库契约强制

- 所有 `/lib/**` 子图 **必须** 声明 `signature`
- 执行器启动时预加载并校验所有标准库
- LLM 生成时，`available_subgraphs` 必须包含 `signature` 信息

### 4.2 权限与沙箱

节点或子图可声明 `permissions`：

```yaml
permissions:
  - tool: web_search → scope: read_only
  - runtime: python3 → allow_imports: [json, re]
  - network: outbound → domains: ["api.example.com"]
```

- 执行器对 `/lib/**` 启用**最小权限沙箱**
- 未授权行为 → 立即终止并跳转 `on_error`

### 4.3 可观测性增强（v3.0 修复与扩展）

所有节点执行后生成结构化 Trace：

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
  "budget_snapshot": { "nodes_used": 5 }
}
```

- 支持导出为 OpenTelemetry 格式

### 4.4 标准库版本与依赖管理

- 路径支持语义化版本：`next: "/lib/human/clarify_intent@v1"`
- 子图可声明依赖：
  ```yaml
  requires:
    - lib: "/lib/reasoning/verify_solution@^1.0"
  ```
- 执行器启动时解析依赖图，拒绝循环或缺失依赖

---

## 五、LLM 生成指令（System Prompt）

> 你是一个**推理与行动架构师**（Reasoning & Action Architect）。你的任务是生成**可执行、可验证的动态 DAG**，包含：
> - **行动流**：调用工具、与人协作
> - **思维流**：假设 → 计算 → 验证
>
> **必须遵守**：
> - 永远不要输出自然语言解释（除非在 `<!-- LLM: ... -->` 或 `<!-- LLM_INTENT: ... -->` 中）
> - **必须输出一个或多个 `### AgenticDSL '/path'` 块**
> - 若任务已完成，请生成 `end` 节点
> - 默认不能生成 `llm_call`（除非父节点授权）
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
- 执行预算剩余：`nodes: {{ budget.nodes_left }}, llm_calls: {{ budget.llm_calls_left }}`
- （训练模式）期望输出：`{{ expected_output }}`

---

## 六、完整示例（v3.0）

```markdown
### AgenticDSL `/__meta__`

yaml
version: "3.0"
execution_budget:
  max_nodes: 20
  max_llm_calls: 2
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
on_error: "/self/repair"

### AgenticDSL `/self/repair`

yaml
type: llm_call
prompt_template: "方程 {{ $.expr }} 求解失败。请重写为标准形式并生成新DAG。"
output_constraints:
  - must_include_signature
next: "/dynamic/repair_123"
```

---

> **AgenticDSL v3.0 是行动智能与推理智能的统一语言。**  
> 它让大模型不仅能“执行任务”，更能“运行思维”，并在实践中不断自我优化——  
> 这正是构建 **AI 原生操作系统** 的基石。
