# AgenticDSL  规范  
**安全 · 可终止 · 可调试 · 可复用 · 可契约 · 可验证**

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

## 二、节点抽象层级（ 核心增强）

AgenticDSL 节点分为三层，确保语义清晰与可演进性：

| 层级 | 说明 | 约束 |
|------|------|------|
| **1. 执行原语层（叶子节点）** | 规范内置、不可扩展的最小操作单元 | 禁止用户自定义新类型 |
| **2. 内存记忆和推理原语层（规范子图）** | 规范提供的稳定内存记忆接口和推理模式实现 | 路径：`/lib/memory/**`, `/lib/reasoning/**`，版本稳定，由规范维护 |
| **3. 知识应用层（标准库子图）** | 用户/社区扩展的领域逻辑 | 路径：`/lib/workflow/**`, `/lib/knowledge/**`，需带 `signature` |

> ✅ **所有复杂逻辑必须通过子图组合实现，禁止在叶子节点中编码高层语义。**

---

## 三、术语表（ 新增）

| 术语 | 定义 |
|------|------|
| **子图（Subgraph）** | 一个以 `### AgenticDSL '/path'` 开头的逻辑单元，可被其他节点调用 |
| **动态生长（Dynamic Growth）** | 通过 `generate_subgraph` 节点在运行时生成新子图并注册到 `/dynamic/**` |
| **契约（Contract）** | 由 `signature` 定义的输入/输出接口规范，用于调用前校验与调用后验证 |
| **软终止（Soft Termination）** | 子图执行结束时返回调用者上下文，而非终止整个 DAG |
| **核心标准库（Core SDK）** |  强制要求实现的 `/lib/**` 子图集合（见附录 C） |
| **执行原语层** | 内置叶子节点（如 `assign`, `assert`），不可扩展 |
| **推理原语层** | 规范维护的 `/lib/reasoning/**` 子图，实现通用推理模式 |
| **内存记忆原语层** | 规范维护的 `/lib/memory/**` 子图，实现通用记忆API接口 |

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
| **`array_concat`** | **数组拼接**（保留顺序，允许重复） |
| **`array_merge_unique`** | 数组拼接 + 去重（基于 JSON 序列化值） |

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

### 5.7 `generate_subgraph`
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
    - `/lib/memory/**`：内存记忆原语（规范维护）
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

### 8.5 开发模式支持

- 在 `/__meta__` 中声明 `mode: dev | prod`
- **开发模式**（`dev`）：
  - 默认 `signature_validation: warn`
  - 允许 `last_write_wins`
  - Trace 包含详细上下文快照
- **生产模式**（`prod`，默认）：
  - 强制 `signature_validation: strict`
  - 禁用 `last_write_wins`
  - 最小权限沙箱强制启用

### 8.6 Trace 增强（可观测性）

在 `mode: dev` 下，Trace 必须包含快照信息（若存在）：

```json
{
  "node_path": "/main/solve",
  "ctx_snapshot_available": true,
  "ctx_snapshot_key": "/main/solve",
  "context_snapshot": { ... }  // 可选，若 budget 允许
}
```


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

## 十、Context 快照机制规范（执行器层）

### 10.1 新增上下文只读字段：`$.ctx_snapshots`

执行器必须在运行时维护一个**只读映射** `$.ctx_snapshots`，其结构为：

```json
{
  "ctx_snapshots": {
    "/main/step3": { /* 完整上下文快照 */ },
    "/lib/reasoning/hypothesis_test@v1": { /* 快照 */ }
  }
}
```

- **键（key）**：触发快照的**节点路径**（如 `/main/solve`）
- **值（value）**：该节点**执行前**的完整上下文副本（深拷贝）
- **访问权限**：只读。任何 `assign`、`tool_call` 等节点**不得写入** `$.ctx_snapshots`
- **生命周期**：随 DAG 执行结束自动销毁

### 10.2 快照触发策略（自动、可配置）

执行器**自动**在以下节点类型执行前保存快照（仅当 `mode: dev` 或显式启用）：

| 节点类型 | 触发条件 |
|--------|--------|
| `fork` | 总是触发（分支探索前） |
| `generate_subgraph` | 总是触发（动态生成前） |
| `assert` | 总是触发（验证前） |
| `tool_call` / `codelet_call` | **仅当声明 `rollback_on_failure: true`** |
| 其他节点 | 不触发（除非通过元指令显式请求） |

> ✅ **生产模式（`mode: prod`）默认禁用快照**，可通过 `execution_budget.enable_snapshots: true` 显式开启。

### 10.3 快照资源控制

快照受全局预算约束：

```yaml
### AgenticDSL `/__meta__`
execution_budget:
  max_snapshots: 5        # 默认：dev=10, prod=0
  snapshot_max_size_kb: 512  # 单快照最大体积（压缩后）
```

- 超出 `max_snapshots` 时，**按 FIFO 策略丢弃最早快照**
- 快照序列化必须使用紧凑 JSON（禁止格式化空格）

### 10.4 快照恢复方式（通过标准 `assign`）

用户可通过 `assign` 节点恢复快照（通常在 `on_failure` 路径中）：

```yaml
type: assign
assign:
  expr: "{{ $.ctx_snapshots['/main/step3'] }}"
  path: ""  # 全量覆盖上下文
# 或
assign:
  expr: "{{ $.ctx_snapshots['/main/step3'].user_input }}"
  path: "user_input"  # 部分恢复
```

> ⚠️ **安全限制**：表达式中对 `$.ctx_snapshots` 的访问必须为**静态字符串键**（禁止动态计算键名），防止信息泄露。

---

## 十一、推理原语层

以下为规范推荐的 **带回溯能力的推理原语子图**，应纳入 `/lib/reasoning/**`。

### 11.1 子图：`/lib/reasoning/with_rollback`

#### AgenticDSL `/lib/reasoning/with_rollback`
```yaml
signature:
  inputs:
    - name: try_path
      type: string
      description: "主尝试路径（如 '/dynamic/solve_attempt'）"
      required: true
    - name: fallback_path
      type: string
      description: "回溯后执行路径"
      required: true
    - name: checkpoint_node
      type: string
      description: "用于恢复的快照节点路径（默认为本节点路径）"
      required: false
  outputs:
    - name: success
      type: boolean
      required: true
  version: "1.0"
  stability: stable

# 自动触发快照（因是 assert 类节点）
type: assert
condition: "true"  # 无条件通过，仅用于触发快照
on_failure: "/self/never_called"  # 占位

next: "{{ $.try_path }}"
```

#### AgenticDSL `/lib/reasoning/with_rollback/fallback`
```yaml
# 此节点在 try_path 失败后由调用者跳转至此
type: assign
assign:
  expr: "{{ $.ctx_snapshots['{{ $.checkpoint_node or \"/lib/reasoning/with_rollback\" }}'] }}"
  path: ""  # 恢复上下文
next: "{{ $.fallback_path }}"
```

### 使用示例：

#### AgenticDSL `/main/task`
```yaml
type: generate_subgraph
prompt_template: "尝试解方程 {{ $.expr }}"
next: "/lib/reasoning/with_rollback@v1"

### AgenticDSL `/lib/reasoning/with_rollback@v1`
# 自动保存快照 at /lib/reasoning/with_rollback
# 执行 /dynamic/solve_attempt
# 若失败，跳转至 /main/task/on_failure → 调用 fallback 子图
```


支持多假设并行探索，失败分支自动回退：

#### AgenticDSL `/lib/reasoning/hypothesis_branch`
```yaml
signature:
  inputs:
    - name: hypotheses
      type: array
      items: { type: string }  # 子图路径列表
      required: true
  outputs:
    - name: selected_hypothesis
      type: string
      required: true
  version: "1.0"
  stability: experimental

type: fork
fork:
  branches: "{{ $.hypotheses }}"
context_merge_policy:
  "hypothesis_result": error_on_conflict  # 仅允许一个成功
on_failure: "/self/rollback_all"
```

##### AgenticDSL `/lib/reasoning/hypothesis_branch/rollback_all`
```yaml
# 清理所有分支写入，恢复到 fork 前状态
type: assign
assign:
  expr: "{{ $.ctx_snapshots['/lib/reasoning/hypothesis_branch'] }}"
  path: ""
next: "/self/fallback_strategy"
```


---

## 十二、内存记忆原语

统一管理可复用的安全保障的上下文路径（如 `user.plan.date` vs `state.travel.departure`），定义标准输入/输出契约，通过可验证的记忆子图提供标准化的记忆接口。


| 原则 | 实现方式 |
|------|--------|
| **契约驱动** | 所有 `/lib/memory/**` 子图必须声明 `signature` |
| **最小权限** | 显式声明 `permissions`（如 `memory: state_write`） |
| **三层抽象对齐** | 作为 **知识应用层**（`/lib/knowledge/**` 的子集） |
| **可终止 & 可观测** | 每个操作生成结构化 Trace，含 `memory_op_type` |
| **向后兼容** | 不修改现有执行原语，仅扩展标准库 |


### 12.1 核心接口定义（Core Memory SDK）

所有子图路径位于 `/lib/memory/**`，稳定性默认为 `stable`（除非注明 `experimental`）。

#### 12.1.1 结构化状态管理（中期记忆）

##### AgenticDSL `/lib/memory/state/set@v1`
```yaml
signature:
  inputs:
    - name: key
      type: string
      description: "状态路径，如 'travel.departure_date'"
      required: true
    - name: value
      type: any
      required: true
  outputs:
    - name: success
      type: boolean
      required: true
  version: "1.0"
  stability: stable
permissions:
  - memory: state_write
type: assign
assign:
  expr: "{{ $.value }}"
  path: "memory.state.{{ $.key }}"
context_merge_policy:
  "memory.state.{{ $.key }}": last_write_wins
```

##### AgenticDSL `/lib/memory/state/get_latest@v1`
```yaml
signature:
  inputs:
    - name: key
      type: string
      required: true
  outputs:
    - name: value
      type: any
      required: false  # 可能为空
  version: "1.0"
type: assign
assign:
  expr: "{{ $.memory.state[key] | default(null) }}"
  path: "result.value"
```

#### 12.1.2 时间知识图谱操作（中期+长期）

> 注：实际存储由外部系统（如 Graphiti）实现，本子图仅封装调用。

##### AgenticDSL `/lib/memory/kg/write_fact@v1`
```yaml
signature:
  inputs:
    - name: head
      type: string
      required: true
    - name: relation
      type: string
      required: true
    - name: tail
      type: any
      required: true
    - name: timestamp
      type: string
      format: "ISO8601"
      required: false  # 默认为 $.now
  outputs:
    - name: fact_id
      type: string
  version: "1.0"
permissions:
  - kg: temporal_fact_insert
type: tool_call
tool: kg_write_fact
arguments:
  head: "{{ $.head }}"
  relation: "{{ $.relation }}"
  tail: "{{ $.tail }}"
  timestamp: "{{ $.timestamp or $.now }}"
output_mapping:
  fact_id: "result.fact_id"
```

##### AgenticDSL `/lib/memory/kg/query_latest@v1`
```yaml
signature:
  inputs:
    - name: head
      type: string
      required: true
    - name: relation
      type: string
      required: true
  outputs:
    - name: tail
      type: any
    - name: timestamp
      type: string
  version: "1.0"
permissions:
  - kg: temporal_fact_read
type: tool_call
tool: kg_query_latest
arguments:
  head: "{{ $.head }}"
  relation: "{{ $.relation }}"
output_mapping:
  tail: "result.tail"
  timestamp: "result.timestamp"
```

#### 12.1.3 语义记忆操作（长期记忆）

##### AgenticDSL `/lib/memory/vector/store@v1`
```yaml
signature:
  inputs:
    - name: text
      type: string
      required: true
    - name: metadata
      type: object
      required: false
      schema: { type: object }
  outputs:
    - name: success
      type: boolean
  version: "1.0"
permissions:
  - vector: store
type: tool_call
tool: vector_store
arguments:
  text: "{{ $.text }}"
  metadata:
    user_id: "{{ $.user.id }}"
    timestamp: "{{ $.now }}"
    task_id: "{{ $.task.id }}"
    extra: "{{ $.metadata | default({}) }}"
output_mapping:
  success: "result.success"
```

##### AgenticDSL `/lib/memory/vector/recall@v1`
```yaml
signature:
  inputs:
    - name: query
      type: string
      required: true
    - name: top_k
      type: integer
      default: 3
  outputs:
    - name: memories
      type: array
      schema:
        type: array
        items:
          type: object
          properties:
            text: { type: string }
            score: { type: number }
            metadata: { type: object }
  version: "1.0"
permissions:
  - vector: recall
type: tool_call
tool: vector_recall
arguments:
  query: "{{ $.query }}"
  top_k: "{{ $.top_k }}"
  filter:
    user_id: "{{ $.user.id }}"
output_mapping:
  memories: "result.memories"
```


#### 11.1.4 用户画像管理（长期记忆）

##### AgenticDSL `/lib/memory/profile/update@v1`
```yaml
signature:
  inputs:
    - name: attributes
      type: object
      required: true
      schema: { type: object }
  outputs:
    - name: success
      type: boolean
  version: "1.0"
permissions:
  - profile: update
type: tool_call
tool: profile_update
arguments:
  user_id: "{{ $.user.id }}"
  attributes: "{{ $.attributes }}"
output_mapping:
  success: "result.success"
```

##### AgenticDSL `/lib/memory/profile/get@v1`
```yaml
signature:
  inputs: []
  outputs:
    - name: profile
      type: object
      schema: { type: object }
  version: "1.0"
permissions:
  - profile: read
type: tool_call
tool: profile_get
arguments:
  user_id: "{{ $.user.id }}"
output_mapping:
  profile: "result.profile"
```



### 12.2 权限模型（Permissions Schema）

| 权限声明 | 说明 | 最小权限范围 |
|--------|------|------------|
| `memory: state_write` | 写入 `memory.state.*` | 仅限 Context 写入 |
| `kg: temporal_fact_insert` | 插入时间事实 | 仅限当前用户图谱 |
| `kg: temporal_fact_read` | 查询时间事实 | 仅限当前用户 |
| `vector: store` | 存储语义记忆 | 自动附加 `user_id` |
| `vector: recall` | 检索语义记忆 | 自动过滤 `user_id` |
| `profile: update` | 更新用户画像 | 仅限当前用户 |
| `profile: read` | 读取用户画像 | 仅限当前用户 |

> ✅ 执行器必须在调度前验证权限，未授权 → 跳转 `on_error`。


### 12.3 工具注册要求（Tool Registration）

为支持上述子图，执行器必须预注册以下工具（由开发者实现）：

| 工具名 | 输入 | 输出 | 参考实现 |
|-------|------|------|--------|
| `kg_write_fact` | `{head, relation, tail, timestamp}` | `{fact_id}` | Graphiti / Cognee Adapter |
| `kg_query_latest` | `{head, relation}` | `{tail, timestamp}` | Graphiti / Neo4j Cypher |
| `vector_store` | `{text, metadata}` | `{success}` | LightRAG + Qdrant/FAISS |
| `vector_recall` | `{query, top_k, filter}` | `{memories[]}` | LightRAG Retriever |
| `profile_update` | `{user_id, attributes}` | `{success}` | Mem0 API Wrapper |
| `profile_get` | `{user_id}` | `{profile}` | Mem0 API Wrapper |

> 🔧 工具实现**不要求**纳入规范，但**接口契约必须一致**。
> 通过 **标准化记忆调用语义**，使 AgenticDSL 应用能够：
- **安全地** 使用混合记忆；
- **无需重复造轮子**；
- **无缝切换记忆后端**；
- **支持 LLM 自动生成记忆逻辑**。

### 12.4 可观测性（Trace Schema 扩展）

所有记忆操作 Trace 必须包含：

```json
{
  "memory_op_type": "state_set | kg_write | vector_store | profile_update",
  "memory_key": "travel.departure_date",
  "backend_used": "context | graphiti | qdrant | mem0",
  "latency_ms": 12,
  "user_id": "user_123"
}
```

### 12.5 示例：订票助手使用标准记忆接口

#### AgenticDSL '/main/booking'
```yaml
type: assign
assign:
  expr: "2025-11-20"
  path: "user_input.date"
next: "/lib/memory/state/set@v1?key=travel.departure_date&value={{ $.user_input.date }}"
```

#### AgenticDSL '/main/confirm'
```yaml
type: assign
assign:
  expr: "已记录您的出发日期为 {{ $.memory.state.travel.departure_date }}"
  path: "response.text"
next: "/end"
```

> ✅ 应用层无需关心记忆后端，仅依赖标准接口。

---


## 十三、完整示例

### AgenticDSL `/__meta__`
```yaml
version: "1.0"
mode: dev  # ✅ 开发模式
execution_budget:
  max_nodes: 20
  max_subgraph_depth: 2
  max_duration_sec: 30
context_merge_strategy: "error_on_conflict"
```

### AgenticDSL `/main/solve_equation`
```yaml
type: assign
assign:
  expr: "x^2 + 2x + 1 = 0"
next: "/lib/reasoning/solve_quadratic@v1"
```

### AgenticDSL `/main/verify`
```yaml
type: assert
condition: "len($.roots) == 1 and $.roots[0] == -1"
expected_output:
  roots: [-1]
on_success: "archive_to('/lib/solved/quadratic@v1')"
on_failure: "/self/repair"
```

### AgenticDSL `/self/repair`
```yaml
type: generate_subgraph
prompt_template: "方程 {{ $.expr }} 求解失败。请重写为标准形式并生成新DAG。"
signature_validation: warn
on_signature_violation: "/self/fallback"
next: "/dynamic/repair_123"
```

### AgenticDSL `/lib/human/approval`  # ✅ Core SDK 示例
```yaml
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

## 附录 C：核心标准库（Core SDK） 必须实现清单

| 路径 | 用途 | 稳定性 |
|------|------|--------|
| `/lib/reasoning/assert` | 中间结论验证 | stable |
| `/lib/human/clarify_intent` | 请求用户澄清意图 | stable |
| `/lib/human/approval` | 人工审批节点 | stable |
| `/lib/workflow/parallel_map` | 基于 `fork` 的 map 封装 | experimental |
| `/lib/reasoning/solve_quadratic` | 二次方程求解示例 | experimental |

> 执行器必须预加载并校验以上子图。社区可扩展，但不得修改其 `signature`。

---

## 附录 D：记忆原语层的演进路线

- 6 个核心子图（`set`, `get_latest`, `write_fact`, `query_latest`, `store`, `recall`, `update`, `get`）
- 实验性：
  - `/lib/memory/orchestrator/hybrid_recall@v1`（融合结构化+语义）
  - 支持记忆 TTL（`assign` + `$.now` + 过期策略）


### 与现有系统的映射

| AgenticDSL 接口 | 推荐后端实现 |
|----------------|------------|
| `/lib/memory/state/**` | Context（内存） |
| `/lib/memory/kg/**` | Graphiti（首选）、Cognee |
| `/lib/memory/vector/**` | LightRAG + Qdrant/FAISS |
| `/lib/memory/profile/**` | Mem0 |


---  

> **AgenticDSL  是迈向 AI 原生操作系统的坚实一步。**  
> 它不仅定义了“如何运行思维”，更通过 **三层抽象 + Core SDK + 开发模式 + JSON Schema 契约**，  
> 为构建**可靠、可协作、可进化的智能体生态**提供了工程基石。

