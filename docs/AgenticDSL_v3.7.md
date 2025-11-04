# AgenticDSL 规范 v3.7
**安全 · 可终止 · 可调试 · 可复用 · 可契约 · 可验证**  

> “优秀的 DSL 不是让机器更容易执行，而是让人类更容易表达意图，同时让机器能够可靠地验证这种意图。”  
> — AgenticDSL 核心哲学

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

| 角色       | 职责                             |
|------------|----------------------------------|
| LLM        | 程序员：基于真实状态生成可验证子图 |
| 执行器     | 运行时：确定性调度、状态合并、预算控制 |
| 上下文     | 内存：结构可契约、合并可策略、冲突可诊断 |
| DAG        | 程序：图可增量演化，支持行动流与思维流 |
| 标准库     | SDK：`/lib/**` 必须带 `signature`，最小权限沙箱 |

### 1.3 设计原则

- **确定性优先**：所有节点必须在有限时间内完成；禁止异步回调；LLM 调用必须声明 `seed` 与 `temperature`；输出需经结构化验证（如 JSON Schema）
- **契约驱动**：接口必须声明，调用必须验证
- **最小权限**：节点/子图需显式声明所需权限；权限组合遵循**交集原则**
- **可终止性**：全局预算控制，防止无限循环或生成
- **可观测性**：每个节点生成结构化 Trace，支持调试与训练

---

## 二、节点抽象层级（三层架构 + 交互边界）

AgenticDSL 节点分为三层，确保语义清晰与可演进性：

| 层级                     | 说明                          | 约束                          |
|--------------------------|-------------------------------|-------------------------------|
| 1. 执行原语层（叶子节点） | 规范内置、不可扩展的最小操作单元 | 禁止用户自定义新类型          |
| 2. 标准原语层             | 规范提供的稳定接口实现         | 路径：`/lib/dslgraph/**`, `/lib/memory/**`, `/lib/reasoning/**`, `/lib/conversation/**`，版本稳定 |
| 3. 知识应用层             | 用户/社区扩展的领域逻辑        | 路径：`/lib/workflow/**`, `/lib/knowledge/**` |

✅ **所有复杂逻辑必须通过子图组合实现，禁止在叶子节点中编码高层语义。**

### 2.1 层间契约规则
- **执行 → 标准原语**：仅通过上下文传递数据，禁止直接 API 调用  
- **标准原语 → 知识应用**：必须通过 `signature` 暴露能力  
- **禁止跨层跳转**：知识应用层不得直接调用执行原语层（必须通过 `/lib/**` 封装）
- **动态子图生成能力**：通过 `/lib/dslgraph/**` 实现，`llm_generate_dsl` 仅用于内部封装。

### 2.2 适配器模式显式化  
所有外部系统交互必须通过规范定义的工具接口：
- **工具注册表**：执行器维护 `tool_schema`，声明输入/输出契约  
- **适配器隔离**：DAG 仅通过 `tool_call` 与工具交互，不依赖实现细节  

---

## 三、术语表

| 术语             | 定义                                                                 |
|------------------|----------------------------------------------------------------------|
| 子图（Subgraph） | 以 `### AgenticDSL '/path'` 开头的逻辑单元                           |
| 动态生长         | 通过子图生成在运行时注册新子图至 `/dynamic/**`                        |
| 契约（Contract） | 由 `signature` 定义的输入/输出接口规范                               |
| 软终止           | 子图结束时返回调用者上下文，而非终止整个 DAG                         |
| 核心标准库       | 强制实现的 `/lib/**` 子图集合（见附录 C）                            |
| 执行原语层       | 内置叶子节点（如 `assign`, `assert`），不可扩展                      |

---

## 四、公共契约

### 4.1 上下文模型（Context）

全局可变字典，支持嵌套路径（如 `user.name`）

**合并策略（字段级、可继承）**

| 策略                    | 行为说明                                                                 |
|-------------------------|--------------------------------------------------------------------------|
| `error_on_conflict`（默认） | 任一字段在多个分支中被写入 → 报错终止                                     |
| `last_write_wins`       | 以最后完成的节点写入值为准（仅用于幂等操作，**禁用于 `prod` 模式**）     |
| `deep_merge`            | 递归合并对象；**数组完全替换**（非拼接）；标量覆盖（严格遵循 RFC 7396） |
| `array_concat`          | 数组拼接（保留顺序，允许重复）                                            |
| `array_merge_unique`    | 数组拼接 + 去重（基于 JSON 序列化值）                                     |

✅ **字段级策略继承**：支持通配路径（如 `results.*`），子图策略优先于父图  
✅ **结构化冲突错误**：必须包含字段路径、各分支值、来源节点、错误码 `ERR_CTX_MERGE_CONFLICT`

### 4.2 Inja 模板引擎（安全模式）

✅ 允许：变量、条件、循环、表达式  
❌ 禁止：`include`/`extends`、环境变量、任意代码执行  
🔁 性能优化：缓存相同模板+上下文的渲染结果

### 4.3 节点通用字段

| 字段                | 说明                                                                 |
|---------------------|----------------------------------------------------------------------|
| `type`              | 节点类型（必需）                                                    |
| `next`              | 路径或路径列表（支持 `@v1`）                                         |
| `permissions`       | 权限声明（见 7.2）                                                  |
| `context_merge_policy` | 字段级合并策略                                                       |
| `on_success`        | 成功后动作（如 `archive_to(...)`）                                   |
| `expected_output`   | 期望输出（用于验证/训练）                                            |
| `curriculum_level`  | 课程难度标签（如 `beginner`）                                        |

❌ 移除 `dev_comment`：使用标准 Markdown 注释（如 `<!-- debug: ... -->`）

---

## 五、核心叶子节点定义（执行原语层）

### 5.1 `assign`
- **语义**：安全赋值到上下文（Inja 表达式）

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `assign.expr` | string | ✅ | Inja 表达式 |
| `assign.path` | string | ✅ | 目标上下文路径 |
| `meta.ttl_seconds` | integer | ❌ | 字段存活时间（秒），超时后自动删除 |
| `meta.persistence` | string | ❌ | `ephemeral`（默认）或 `durable` |

示例：
```yaml
type: assign
assign:
  expr: "coffee"
  path: "memory.state.preferred_drink"
meta:
  ttl_seconds: 600
  persistence: ephemeral
```

**执行器行为**：
- 记录该字段的过期时间戳（`now + ttl_seconds`）
- 每次调度前清理已过期字段（不计入 `max_nodes`）
- `durable` 字段永不自动清理（用于长期状态）

> ⚠️ 仅 `memory.state.*` 路径支持 TTL；其他路径忽略 `meta`


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

### 5.7 `llm_generate_dsl`

**语义**：委托 LLM 生成一个或多个结构化的 AgenticDSL 动态子图（DAG 片段），仅负责最小安全生成，不包含权限/签名/修复等高层逻辑。

**关键字段**：

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `prompt` | string | ✅ | 完整提示（已渲染 Inja 模板） |
| `llm` | object | ✅ | `model`, `seed`（整数）, `temperature`（≤1.0） |
| `output_constraints` | object | ✅ | 见下表 |
| `next` | string or list | ✅ | 成功后跳转路径 |

**`output_constraints` 子字段**：

| 字段 | 默认值 | 说明 |
|------|--------|------|
| `must_be_agenticdsl_blocks` | true | LLM 输出必须为 `### AgenticDSL '/dynamic/...'` 块 |
| `namespace_prefix` | `"/dynamic/"` | 强制生成路径前缀，禁止 `/lib/` 或 `/main/` |
| `max_blocks` | 3 | 最多生成子图数量 |
| `validate_json_schema` | true | 对每个 block 内容做 JSON Schema 验证 |

**权限要求**：

```yaml
permissions:
  - generate_subgraph: { max_depth: N }  # N ≥ 1，必须在资源声明中允许
```

**行为规则**：

- LLM 可以输出自然语言。输出中可包含多个Markdown任意级别标题 `### AgenticDSL '/dynamic/...'` 块。
- 生成的子图自动注册至 `/dynamic/...`，可在后续节点通过 `next` 调用
- 验证失败 → 跳转 `on_failure`（若未定义，则终止）
- **不执行签名验证**（由上层子图处理）

 **Trace 输出**：

```json
{
  "llm_generate_dsl": {
    "prompt_tokens": 120,
    "completion_tokens": 300,
    "generated_paths": ["/dynamic/plan_1", "/dynamic/plan_2"],
    "validation_passed": true
  }
}
```


### 5.8 `start`
- `start`：无操作，跳转到 `next`

---

## 六、统一文档结构

### 6.1 路径命名空间（关键强化）

- `/lib/**`：**只读命名空间**，禁止任何运行时写入或覆盖  
  - `/lib/dslgraph/**`：AgenticDSL 图结构操作（生成、验证、归档、组合）
  - `/lib/reasoning/**`：推理原语  
  - `/lib/memory/**`：记忆原语  
  - `/lib/workflow/**`、`/lib/knowledge/**`：通用应用  
  - `/lib/conversation/**`：对话协议（话题/角色/会议）  
- `/dynamic/**`：运行时生成  
- `/main/**`：主流程  

> 违反命名空间写规则 → `ERR_NAMESPACE_VIOLATION`

### 6.2 子图签名（Subgraph Signature）

所有 `/lib/**` 必须声明：

```yaml
signature:
  inputs:
    - name: expr
      type: string
      required: true
  outputs:
    - name: roots
      type: array
      schema: { type: array, items: { type: number }, minItems: 1 }
  version: "1.0"
  stability: stable  # stable / experimental / deprecated
```

### 6.3 显式执行入口

#### AgenticDSL `/__meta__`
```yaml
version: "3.3"
mode: dev
entry_point: "/main/start"  # ✅ 必需：DAG 执行入口路径
execution_budget:
  max_nodes: 20
  max_subgraph_depth: 2
```

**规则**：
- 唯一性：每个 `.agent.md` 仅允许一个 `entry_point`。
- 与动态生成兼容：`entry_point` 可指向 `/dynamic/**` 节点（例如恢复中断任务），但该节点必须在执行前已注册。
- 必需字段；若缺失 → `ERR_MISSING_ENTRY_POINT`
- 必须指向文档中已定义的子图
- 执行器不再依赖入度计算
- 开发体验：推荐将入口设为 `/main/start`（类型为 `start` 或 `assign`），避免直接从复杂逻辑开始。


### 6.4 资源声明（Resource Declaration）

资源依赖必须通过 **`/__meta__/resources` 块**进行**声明式注册**，该块为**非执行节点**，仅用于 DAG 启动前的资源可用性验证。资源声明归属元信息，与 `budget`、`mode` 同级

#### AgenticDSL `/__meta__/resources`
```yaml
type: resource_declare
resources:
  - type: tool
    name: web_search
    scope: read_only
  - type: runtime
    name: python3
    allow_imports: [json, sympy]
  - type: network
    outbound:
      domains: ["api.mathsolver.com"]
  - type: memory
    backends: [kg, vector]
  - type: knowledge_graph
    capabilities:
      - multi_hop_query
      - evidence_path_extraction
      - subgraph_write
  - type: generate_subgraph
    max_depth: 2
  - type: tool
    name: image_generator
    scope: write
    capabilities: [text_to_image, high_res]
    rate_limit: "5/min"
  - type: tool
    name: web_search
    scope: read_only
    capabilities: [search, factual]
```

#### 语义规则：
- **路径固定**：必须为 `/__meta__/resources`
- **非执行性**：不参与 DAG 执行流，不计 `max_nodes`，无 `next` 字段
- **启动时验证**：执行器在 **DAG 启动前** 一次性验证所有声明资源
- **验证失败**：立即终止，返回错误码 `ERR_RESOURCE_UNAVAILABLE`
- **与权限联动**：声明的资源自动成为后续节点权限检查的上下文依据

#### 行为规则:
- **能力声明**：声明所需能力（如 `evidence_path_extraction`），而非具体实现
- **非强制绑定**：`backend_hint` 仅作为优化提示，执行器可选择任意满足能力的后端
- **降级机制**：若未声明所需能力，执行器应：
  1. 尝试使用基础三元组查询（`query_latest`）
  2. 若完全不支持，返回 `ERR_UNSUPPORTED_CAPABILITY`


#### 资源类型定义：

| 类型 | 字段 | 示例 |
|------|------|------|
| `tool` | `name`, `scope`, `capabilities`, `rate_limit` | `image_generator`, `["text_to_image"]`, `"5/min"`, `web_search`, `read_only` |
| `runtime` | `name`, `allow_imports` | `python3`, `[json, re]` |
| `network` | `outbound.domains` | `["api.example.com"]` |
| `memory` | `backends` | `[kg, vector, profile]` |
| `generate_subgraph` | `max_depth` | `2` |

**语义规则补充**：
- `capabilities` 用于语义匹配（如 LLM 选择工具）
- `rate_limit` 供执行器实现调用限流（非强制，但建议支持）


---

## 七、安全与工程保障

### 7.1 标准库契约强制  
启动时预加载并校验所有 `/lib/**`；LLM 生成时 `available_subgraphs` 必须含 `signature`

### 7.2 权限与沙箱

```yaml
permissions:
  - tool: web_search → scope: read_only
  - runtime: python3 → allow_imports: [json, re]
  - network: outbound → domains: ["api.example.com"]
  - generate_subgraph: { max_depth: 2 }
```

✅ **权限组合规则**：
- **交集原则**：节点权限 ∩ 父上下文授权权限  
- **拒绝优先**：任一缺失 → 跳转 `on_error`  
- **权限降级**：子图调用时权限**只能减少**

> **资源声明是权限的前置契约**。执行器在启动时验证 `/__meta__/resources` 中声明的资源可用性后，才允许执行声明了对应 `permissions` 的节点。

### 7.3 可观测性（Trace Schema）  
兼容 OpenTelemetry，包含：执行状态、上下文变更、输出匹配、LLM 意图、预算快照等

```json
{
  "reasoning_evidence": {
    "type": "graph_based",
    "evidence_type": "path_based",  // 路径/子图/向量
    "paths": [
      [
        { "head": "Beijing", "relation": "capital_of", "tail": "China" },
        { "head": "China", "relation": "located_in", "tail": "Asia" }
      ]
    ],
    "confidence_scores": [0.94, 0.87],
    "backend_used": "gfm-retriever-v1",
    "subgraph_id": "sg-20251103-abc"
  }
}
```
**记录规则**：
- 仅当调用图原生接口时记录
- 所有字段均为可选，执行器按能力填充
- `backend_used` 必须记录实际使用的后端标识，便于调试


### 7.4 标准库版本与依赖管理  
- 路径支持语义化版本：`/lib/...@v1`  
- 子图可声明依赖：`requires: - lib: "/lib/reasoning/...@^1.0"`  
- 执行器启动时解析依赖图，拒绝循环或缺失依赖

---

## 八、核心能力规范

### 8.1 动态 DAG 执行 + 全局预算

> **DAG 启动流程**：
> 1. 解析所有子图块（包括 `/__meta__`、`/main/**`、`/lib/**`）
> 2. **验证 `/__meta__/resources`**（若存在）
> 3. 验证 `/lib/**` 签名与依赖
> 4. 检查 `entry_point` 存在性
> 5. 启动调度器

- `execution_budget`：`max_nodes`, `max_subgraph_depth`, `max_duration_sec`
- 超限 → 跳转 `/__system__/budget_exceeded`
- **终止条件**：队列空 + 无活跃生成 + 无待合并子图 + 预算未超


### 8.2 动态子图生成（`/lib/dslgraph/generate@v1` 核心机制）

- LLM 必须输出一个或多个 `### AgenticDSL '/dynamic/...'` markdown块  
- 执行器解析为子图对象，注册到 `/dynamic/**`  
- 若声明 `signature`，则按 `signature_validation` 策略校验  
- 新子图可被后续节点通过 `next: "/dynamic/plan_123"` 调用

> **禁止行为**：LLM 生成的子图不得包含 `/lib/**` 写入或调用未声明工具

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

### 8.5 开发模式  
- 在 `/__meta__` 中声明 `mode: dev | prod`
- `dev`：`signature_validation: warn`，允许 `last_write_wins`，含上下文快照  
- `prod`（默认）：强制 `strict`，禁用 `last_write_wins`，最小权限沙箱启用  

Trace 增强:
- `dev` 模式下包含快照信息（若 budget 允许）


### 8.6 性能边界指南（新增）

- **上下文大小**：<1MB（>512KB 启用快照压缩）  
- **单子图节点数**：<50  
- **预算建议**：`max_nodes: 10 × [预期分支数]`，`max_subgraph_depth: 3`  


---

## 九、LLM 生成指令（System Prompt）

你是一个**推理与行动架构师**，你的任务是生成**可执行、可验证的动态 DAG**，包含：
- **行动流**：调用工具、与人协作
- **思维流**：假设 → 计算 → 验证

你必须：
- 输出一个或多个 `### AgenticDSL '/path'` 块  
- 遵守预算：递归深度 ≤ `{{ budget.subgraph_depth_left }}`  
- 优先调用标准库（清单含 `signature`）  
- 你可生成 `generate_subgraph` 节点，但总递归深度不得超过 {{ budget.subgraph_depth_left }}
- 你应优先调用 `/lib/dslgraph/generate@v1` 生成新子图，而非直接使用底层原语。  
- 若需精细控制 LLM 输出，可通过 `prompt_template` 传递约束。
- 若任务已完成，请生成 `end` 节点
- **所有 LLM 调用必须包含 `seed` 与 `temperature`**

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

- 生成子图时可参考 `available_resources`

---

## 十、标准原语层

### 10.1 子图管理（`/lib/dslgraph/**`）

#### AgenticDSL `/lib/dslgraph/generate@v1`（stable）
```yaml
signature:
  inputs:
    - name: prompt_template
      type: string
      required: true
    - name: signature_validation
      type: string
      enum: [strict, warn, ignore]
      default: "strict"
    - name: on_signature_violation
      type: string
    - name: on_generation_failure
      type: string
    - name: archive_on_success
      type: string
  outputs:
    - name: generated_paths
      type: array
    - name: success
      type: boolean
version: "1.0"
stability: stable
permissions:
  - generate_subgraph: { max_depth: 2 }
```

> ⚠️ **内部实现示意**：以下 DAG 仅为说明逻辑，实际由执行器内置实现，用户不可修改。

```yaml
# Step 1: 渲染提示 → Step 2: 调用 llm_generate_dsl → Step 3: 签名验证 → Step 4: 归档
```yaml
# Step 1: 渲染提示
type: assign
assign:
  expr: "{{ prompt_template | render_with_context }}"
  path: "internal.prompt"
next: "/self/call_llm"

# Step 2: 调用底层原语
AgenticDSL `/self/call_llm`
type: llm_generate_dsl
prompt: "{{ $.internal.prompt }}"
llm:
  model: "gpt-4o"
  seed: "{{ $.llm_seed | default(42) }}"
  temperature: "{{ $.temperature | default(0.3) }}"
output_constraints:
  namespace_prefix: "/dynamic/"
  max_blocks: 3
  validate_json_schema: true
permissions:
  - generate_subgraph: { max_depth: 2 }
on_failure: "{{ $.on_generation_failure or '/self/fallback' }}"
next: "/self/validate_signatures"

# Step 3: 签名验证（若启用）
AgenticDSL `/self/validate_signatures`
type: codelet_call
runtime: internal_dsl_validator
code: |
  for path in dynamic_paths:
    subgraph = get_subgraph(path)
    if 'signature' in subgraph:
      if not validate_signature(subgraph):
        if signature_validation == 'strict':
          raise ERR_SIGNATURE_VIOLATION
        elif signature_validation == 'warn':
          log_warning(...)
# on_violation 跳转由外部处理
next: "/self/archive_or_finish"

# Step 4: 归档（可选）
AgenticDSL `/self/archive_or_finish`
type: assign
assign:
  expr: "{{ $.generated_paths }}"
  path: "result.generated_paths"
next: |
  {% if $.archive_on_success %}
    "/lib/dslgraph/archive_to@v1?target={{ $.archive_on_success }}"
  {% else %}
    "/end?termination_mode=soft&output_keys=[generated_paths, success]"
  {% endif %}
```

#### 权限与资源联动

- **资源声明要求**（在 `/__meta__/resources` 中）：
  ```yaml
  - type: generate_subgraph
    max_depth: 2
  ```
- **权限继承**：生成的 `/dynamic/...` 子图权限 ≤ 当前上下文权限（交集原则）
- **禁止行为**：LLM 生成的子图不得包含 `/lib/**` 写入或调用未声明工具

### 10.2 推理原语（`/lib/reasoning/**`）

#### 10.2.1 生成多个假设并行验证，返回有效假设列表。

##### AgenticDSL `/lib/reasoning/hypothesize_and_verify@v1`（stable）
```yaml
signature:
  inputs:
    - name: generator_path
      type: string
      description: "生成假设列表的子图路径"
    - name: verifier_path
      type: string
      description: "验证单个假设的子图路径"
    - name: max_hypotheses
      type: integer
      default: 3
  outputs:
    - name: verified_hypotheses
      type: array
    - name: best_hypothesis
      type: object
version: "1.0"
stability: stable
```


**内部行为**：
1. 调用 `generator_path` → 输出 `hypotheses[]`
2. `fork` 并行执行 `verifier_path`
3. 聚合结果，过滤失败项
4. 按 `verifier` 输出的 `confidence` 排序（可选）


#### 10.2.2 分步推理，每步后自动断言。

##### AgenticDSL `/lib/reasoning/stepwise_assert@v1`（stable）
```yaml
signature:
  inputs:
    - name: steps
      type: array
      items:
        type: object
        properties:
          reasoner: { type: string }
          assertion: { type: string }
    - name: on_assertion_fail
      type: string
  outputs:
    - name: final_state
      type: object
version: "1.0"
```

#### 10.2.3 反事实推理，对比多场景。

##### AgenticDSL `/lib/reasoning/counterfactual_compare@v1`（experimental）
```yaml
signature:
  inputs:
    - name: base_scenario
    - name: variants
    - name: evaluator_path
  outputs:
    - name: comparison_result
version: "1.0"
stability: experimental
```

#### 10.2.4 自动快照+回溯，降低心智负担。

##### AgenticDSL `/lib/reasoning/try_catch@v1`（stable）
```yaml
signature:
  inputs:
    - name: try_block
      type: string
    - name: catch_block
      type: string
  outputs:
    - name: success
      type: boolean
version: "1.0"
```

**内部实现**：
- 在入口处触发快照（通过 `assert`）
- 失败时自动恢复上下文并跳转 `catch_block`


#### 10.2.5 从成功 Trace 归纳规则并归档。

##### AgenticDSL `/lib/reasoning/induce_and_archive@v1`（stable）
```yaml
signature:
  inputs:
    - name: trace_ids
    - name: pattern_template
    - name: archive_path
  outputs:
    - name: generalized_rule
version: "1.0"
```

#### 10.2.6 图引导推理协议

##### AgenticDSL `/lib/reasoning/graph_guided_hypothesize@v1`（experimental）

```yaml
signature:
  inputs:
    - name: question
      type: string
      required: true
      description: "需要回答的问题"
    - name: kg_context
      type: object
      required: true
      schema:
        type: object
        properties:
          start_entities:
            type: array
            items: { type: string }
            minItems: 1
          query_path:
            type: string
          max_hops:
            type: integer
            default: 3
    - name: max_hypotheses
      type: integer
      default: 3
      minimum: 1
      maximum: 10
      description: "最大假设数量"
  outputs:
    - name: hypotheses
      type: array
      required: true
      items:
        type: object
        properties:
          text:
            type: string
            description: "假设文本"
          evidence_path:
            type: array
            description: "支持该假设的证据路径"
            items:
              type: object
              properties:
                head: { type: string }
                relation: { type: string }
                tail: { type: string }
          confidence:
            type: number
            minimum: 0
            maximum: 1
            description: "假设置信度（0-1）"
      minItems: 0
version: "1.0"
stability: experimental
permissions:
  - kg: subgraph_query
  - reasoning: lmm_generate
```
#### 10.2.7 “意图-计划-执行-反思”（IPER）闭环，用于鲁棒任务执行。
##### AgenticDSL `/lib/reasoning/iper_loop@v1`
```yaml
AgenticDSL `/lib/reasoning/iper_loop@v1`
signature:
  inputs:
    - name: user_intent
      type: string
      required: true
      description: "原始用户请求或任务目标"
    - name: planner_path
      type: string
      required: true
      description: "生成执行计划的子图路径（如 /lib/dslgraph/generate@v1）"
    - name: max_reflections
      type: integer
      default: 3
      minimum: 1
      maximum: 5
      description: "最大反思/重试次数"
  outputs:
    - name: final_result
      type: object
      required: true
      description: "最终成功结果或归因报告"
version: "1.0"
stability: stable
permissions:
  - generate_subgraph: { max_depth: 2 }
```

**内部逻辑（示意）**：
- 调用 `planner_path` 生成 `/dynamic/plan_v1`
- 执行该计划
- 若失败，进入反思：调用 `planner_path` 生成修复计划（注入错误上下文）
- 重复 ≤ `max_reflections` 次
- 成功则返回结果；失败则返回归因

**Trace 扩展**：
```json
{
  "iper": {
    "reflection_count": 2,
    "final_status": "success | failed",
    "last_error": "..."
  }
}
```

### 10.3 内存记忆原语

- **接口与实现分离**：`/lib/memory/**` 仅定义标准契约，不包含任何后端细节
- **能力声明**：通过资源声明描述所需能力，而非绑定具体技术

#### 10.3.1 结构化状态管理（中期记忆）

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

#### 10.3.2 时间知识图谱操作（中期+长期）

注：实际存储由外部系统（如 Graphiti）实现，本子图仅封装调用。


##### AgenticDSL `/lib/memory/kg/query_subgraph@v1`（stable）

```yaml
signature:
  inputs:
    - name: start_entities
      type: array
      items: { type: string }
      required: true
      description: |
        起始实体列表，如 ["Beijing", "Shanghai"]。
        实体名称必须为规范化的知识库标识符。
    - name: query_path
      type: string
      required: true
      description: |
        路径查询模式，语法由执行器定义。
        支持多跳模式（如 "(?x)-[capital_of]->(?y)"），
        但具体语法由适配层解释。
    - name: max_hops
      type: integer
      default: 3
      maximum: 5
      description: "最大跳数，防止资源爆炸"
    - name: evidence_required
      type: boolean
      default: true
      description: "是否要求返回证据路径"
  outputs:
    - name: subgraph
      type: object
      required: true
      schema:
        type: object
        properties:
          nodes:
            type: array
            items:
              type: object
              properties:
                id:
                  type: string
                  description: "节点唯一标识符"
                label:
                  type: string
                  description: "节点显示名称"
                type:
                  type: string
                  description: "节点类型（可选）"
          edges:
            type: array
            items:
              type: object
              properties:
                source:
                  type: string
                  description: "源节点ID"
                target:
                  type: string
                  description: "目标节点ID"
                relation:
                  type: string
                  description: "关系类型"
    - name: explanation_paths
      type: array
      required: false
      items:
        type: array
        items:
          type: object
          properties:
            head:
              type: string
              description: "关系头实体"
            relation:
              type: string
              description: "关系类型"
            tail:
              type: string
              description: "关系尾实体"
      description: |
        可解释推理路径列表。
        仅当 evidence_required=true 且后端支持时返回。
version: "1.0"
stability: stable
permissions:
  - kg: subgraph_query  # 新增权限类型
```

##### AgenticDSL `/lib/memory/kg/write_subgraph@v1`

```yaml
signature:
  inputs:
    - name: subgraph
      type: object
      required: true
      schema:
        type: object
        properties:
          nodes:
            type: array
            minItems: 1
          edges:
            type: array
            minItems: 1
    - name: source
      type: string
      default: "user_provided"
      description: "子图来源标识"
  outputs:
    - name: subgraph_id
      type: string
      required: true
      description: "生成的子图唯一ID"
version: "1.0"
stability: stable
permissions:
  - kg: subgraph_write
```

#### 10.3.3 语义记忆操作（长期记忆）

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

#### 10.3.4 用户画像管理（长期记忆）

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

#### 10.3.5 权限模型（Permissions Schema）

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


#### 10.3.6 工具注册要求（Tool Registration）

为支持上述子图，执行器必须预注册以下工具（由开发者实现）：

| 工具名 | 输入 | 输出 | 参考实现 |
|-------|------|------|--------|
| `vector_store` | `{text, metadata}` | `{success}` | LightRAG + Qdrant/FAISS |
| `vector_recall` | `{query, top_k, filter}` | `{memories[]}` | LightRAG Retriever |
| `profile_update` | `{user_id, attributes}` | `{success}` | Mem0 API Wrapper |
| `profile_get` | `{user_id}` | `{profile}` | Mem0 API Wrapper |

> 🔧 工具实现**不要求**纳入规范，但**接口契约必须一致**。

#### 10.3.7 可观测性（Trace Schema 扩展）

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

### 10.4 世界模型及环境感知原语

TODO： AgenticDSL 感知物理世界的原语

### 10.5 对话交流原语

对话是智能体的核心交互范式。AgenticDSL 通过标准子图库 `/lib/conversation/**` 提供结构化对话协议，**复用记忆与推理原语**，支持：

- 多轮对话状态管理  
- 话题隔离与切换  
- 多角色上下文隔离  
- 会议协作与知识聚合  

所有对话能力均通过 **知识应用层标准子图** 实现，**不引入新执行原语**。

#### 10.5.1 对话上下文模型
- 对话状态通过标准记忆接口管理：
  - 话题变量 → `/lib/memory/state/set`
  - 用户偏好 → `/lib/memory/kg/qeury_subgraph`
  - 画像 → `/lib/memory/profile/update`
- **禁止**在主上下文（如 `$.user_input`）中直接堆叠对话历史

#### 10.5.2 标准对话子图

##### AgenticDSL `/lib/conversation/start_topic@v1`
```yaml
signature:
  inputs:
    - name: topic_id
      type: string
    - name: initial_context
      type: object
  outputs:
    - name: context_path
      type: string  # e.g., "/topics/booking/context"
permissions: [memory: state_write]
```

##### AgenticDSL `/lib/conversation/switch_role@v1`
```yaml
signature:
  inputs:
    - name: role_id
      type: string
  outputs:
    - name: context_path
      type: string  # e.g., "/roles/agent/context"
permissions: [memory: state_write]
```

##### AgenticDSL `/lib/conversation/meeting@v1`
```yaml
signature:
  inputs:
    - name: meeting_id
      type: string
    - name: participants  # role_id list
      type: array
    - name: interaction_mode
      enum: [round_robin, free_discussion, qa_session]
  outputs:
    - name: meeting_summary
      type: object
permissions: [memory: state_write, kg: temporal_fact_insert]
```

#### 10.5.3 设计原则
- 复用 `/lib/memory/state` 存话题状态
- 角色上下文隔离
- 会议共享上下文 + 私有上下文


#### 10.5.4 安全与权限
- **复用现有权限**：`memory: state_write`、`kg: temporal_fact_insert`  
- **上下文隔离**：执行器确保角色 A 无法访问角色 B 的上下文  
- **预算控制**：`max_conversation_turns`、`max_topics`、`max_roles`

#### 10.5.5 Trace 增强
对话节点 Trace 必须包含：
```json
{
  "conversation": {
    "topic_id": "booking",
    "role_id": "agent",
    "turn": 3
  }
}
```

### 10.6 资源工具

#### 10.6.1 动态查询当前可用工具及其能力标签，供 LLM 规划使用。

##### AgenticDSL `/lib/tool/list_available@v1`
```yaml
AgenticDSL `/lib/tool/list_available@v1`
signature:
  inputs:
    - name: required_capabilities
      type: array
      items: { type: string }
      required: false
      description: "所需能力列表（如 ['text_to_image', 'search']）"
  outputs:
    - name: matching_tools
      type: array
      items:
        type: object
        properties:
          name: { type: string }
          capabilities: { type: array, items: { type: string } }
          rate_limit: { type: string }
      required: true
version: "1.0"
stability: stable
permissions: []  # 仅读取元信息，无需运行时权限
```

**行为规则**：
- 从 `/__meta__/resources` 中提取 `type: tool` 条目
- 过滤满足 `required_capabilities` 的工具
- 输出结构化工具清单

> ✅ 此子图可在 LLM prompt 中通过 `{{ available_tools_with_caps }}` 注入

---

## 十一、Context 快照机制规范（安全增强）

### 11.1 快照恢复限制  

```yaml
type: assign
assign:
  expr: "{{ $.ctx_snapshots['/main/step3'] }}"  # ✅ 静态键
  path: ""
```

⚠️ `$.ctx_snapshots` 的访问键**必须为静态字符串**，禁止动态计算（如 `{{ $.key }}`）




---

## 十二、完整示例

### 12.1 基础对话示例

#### AgenticDSL `/__meta__`
```yaml
version: "3.1"
mode: dev
entry_point: "/main/solve_equation"
execution_budget:
  max_nodes: 20
  max_subgraph_depth: 2
  max_duration_sec: 30
context_merge_strategy: "error_on_conflict"
```

#### AgenticDSL `/main/solve_equation`
```yaml
type: assign
assign:
  expr: "x^2 + 2x + 1 = 0"
next: "/lib/reasoning/solve_quadratic@v1"
```

#### AgenticDSL `/main/verify`
```yaml
type: assert
condition: "len($.roots) == 1 and $.roots[0] == -1"
expected_output:
  roots: [-1]
on_success: "archive_to('/lib/solved/quadratic@v1')"
on_failure: "/self/repair"
```

#### AgenticDSL `/self/repair`
```yaml
type: generate_subgraph
prompt_template: "方程 {{ $.expr }} 求解失败。请重写为标准形式并生成新DAG。"
signature_validation: warn
on_signature_violation: "/self/fallback"
next: "/dynamic/repair_123"
```

#### AgenticDSL `/lib/human/approval`  # ✅ Core SDK 示例
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

示例流程说明：

1. **元配置**：启用开发模式，设置预算和合并策略。
2. **主流程**：赋值方程 `x² + 2x + 1 = 0`，调用标准库求解器。
3. **验证**：断言结果应为单根 `-1`；成功则归档，失败则进入修复。
4. **自动修复**：委托 LLM 重写方程并生成新子图（路径 `/dynamic/repair_123`）。
5. **人工审批**（作为 Core SDK 示例）：展示如何通过标准接口请求人类介入。

### 12.2 完整错误处理示例

#### AgenticDSL `/main/equation_solver`
```yaml
type: generate_subgraph
prompt_template: "生成求解 {{ $.expr }} 的DAG子图..."
next: "/lib/reasoning/with_rollback@v1"
on_failure: "/main/equation_solver/fallback"
```

#### AgenticDSL `/main/equation_solver/fallback`
```yaml
type: assign
assign:
  expr: "{{ $.ctx_snapshots['/main/equation_solver'] }}"
  path: ""
next: "/lib/human/clarify_intent@v1"
```

### 12.3 记忆+推理组合示例

#### AgenticDSL `/main/learn_from_failure`
```yaml
type: assign
assign:
  expr: "方程 {{ $.expr }} 求解失败：{{ $.error }}"
next: "/lib/memory/vector/store@v1"
```

### 12.4 话题切换示例

#### AgenticDSL `/main/start`
```yaml
type: assign
assign:
  expr: "我想订机票"
next: "/lib/conversation/start_topic@v1?topic_id=booking"

AgenticDSL `/topics/booking/main`
type: generate_subgraph
prompt_template: "请询问出发地和目的地..."
next: "/lib/memory/state/set@v1?key=booking.dest&value={{ $.user_input }}"
loop_until: "{{ $.booking.confirmed }}"
```

### 12.5 多角色会议示例

#### AgenticDSL `/main/start_meeting`
```yaml
type: assign
assign:
  expr: {
    meeting_id: "support_001",
    participants: ["user", "agent", "tech_expert"],
    interaction_mode: "qa_session"
  }
next: "/lib/conversation/meeting@v1"
```

### 12.6 多假设验证

#### AgenticDSL `/main/solve`
```yaml
type: assign
assign:
  expr: {
    generator: "/dynamic/gen_solutions",
    verifier: "/lib/reasoning/verify_math"
  }
next: "/lib/reasoning/hypothesize_and_verify@v1"
```

### 12.7 图引导多跳问答

#### AgenticDSL `/main/ask_geography`
```yaml
type: assign
assign:
  expr: {
    question: "北京位于哪个大洲？",
    kg_context: {
      start_entities: ["Beijing"],
      query_path: "(?x)-[capital_of|located_in]->*2->(?y)",
      max_hops: 2
    }
  }
next: "/lib/reasoning/graph_guided_hypothesize@v1"
```

#### AgenticDSL `/main/generate_answer`
```yaml
type: assign
assign:
  expr: |
    {% if $.hypotheses|length > 0 and $.hypotheses[0].confidence > 0.7 %}
      {{ $.hypotheses[0].text }} 
      (置信度: {{ "%.2f"|format($.hypotheses[0].confidence) }})
    {% else %}
      无法确定答案，需要更多信息
    {% endif %}
path: "response.text"
next: "/end"
```

**执行流程**：
1. 资源验证：检查是否声明 `multi_hop_query` 和 `evidence_path_extraction` 能力
2. 调用 `graph_guided_hypothesize`：
   - 通过适配层调用后端图查询
   - 生成带证据的假设
3. 生成包含置信度的答案
4. Trace 记录完整的推理证据链




### 12.8 自动修复示例

#### AgenticDSL `/self/repair`
```yaml
type: codelet_call
runtime: compat_v35_generate  # 或直接调用 /lib/dslgraph/generate@v1
arguments:
  prompt_template: "方程 {{ $.expr }} 求解失败。请重写为标准形式并生成新DAG。"
  signature_validation: warn
  on_signature_violation: "/self/fallback"
next: "/dynamic/repair_123"
```

---

## 附录

### 附录 A：最佳实践与约定

**A1. 时间上下文约定（非强制）**
- `$.now`: ISO8601 当前时间（由执行器注入）
- `$.time_anchor`: 任务参考时间点
- `$.timeline[]`: `{ts: "...", event: "...", source: "..."}`

**A2. 禁止行为清单**
- 在 DAG 内实现异步回调
- 在叶子节点中编码高层推理逻辑
- 使用 `generate_subgraph` 调用已有子图
- 输出非 `### AgenticDSL` 块的 LLM 内容
- 在生产模式下使用 `last_write_wins` 合并策略
- 禁止在知识应用层直接使用 `llm_generate_dsl`
- 禁止在 `/lib/dslgraph/**` 之外实现子图生成逻辑

**A3. 推荐开发工作流**：
1. `agentic validate example.agent.md`  
2. `agentic simulate --mode=dev`  
3. 从 Trace 提取失败案例，更新 `expected_output`  
4. 通过 `archive_to` 沉淀验证通过模块  
5. **生产部署必须显式设置 `mode: prod`**

**A5. 资源声明最佳实践**
- 所有对外部能力的依赖（工具、运行时、网络）应在 `/__meta__/resources` 中显式声明
- 避免在 `generate_subgraph` 生成的子图中使用未声明资源
- 生产环境必须完整声明资源，开发环境可适当放宽（但不推荐）


**A6. 性能边界指南**：
- 上下文大小：<1MB（>512KB 启用快照压缩）  
- 单子图节点数：<50  
- 预算配置：`max_nodes: 10 × [预期分支数]`，`max_subgraph_depth: 3`  


### 附录 B：`expected_output` 与 `signature.outputs` 分工

| 机制 | 作用域 | 用途 | 校验时机 |
|------|--------|------|--------|
| `signature.outputs` | **子图接口** | 契约：调用者与被调用者约定 | 调用前（输入）、调用后（输出） |
| `expected_output` | **单次执行** | 验证：本次任务期望的具体值 | 执行后（Trace 记录，可选告警） |

### 附录 C：核心标准库清单（**更新**）

| 路径 | 用途 | 稳定性 |
|------|------|--------|
| `/lib/dslgraph/generate@v1` | 安全生成动态子图 | stable |
| `/lib/reasoning/assert` | 中间结论验证 | stable |
| `/lib/reasoning/hypothesize_and_verify` | 多假设验证 | stable |
| `/lib/reasoning/try_catch` | 异常回溯 | stable |
| `/lib/reasoning/stepwise_assert` | 分步断言 | stable |
| `/lib/reasoning/graph_guided_hypothesize` | 图引导假设生成 | experimental |
| `/lib/human/clarify_intent` | 请求用户澄清意图 | stable |
| `/lib/human/approval` | 人工审批节点 | stable |
| `/lib/workflow/parallel_map` | 基于 `fork` 的 map 封装 | experimental |
| `/lib/conversation/start_topic` | 开启新对话话题 | stable |
| `/lib/conversation/switch_role` | 切换对话角色上下文 | stable |
| `/lib/conversation/meeting` | 多角色会议协调 | stable |
| `/lib/memory/state/**` | Context（内存） |
| `/lib/memory/kg/**` | Graphiti（首选）、Cognee |
| `/lib/memory/vector/**` | LightRAG + Qdrant/FAISS |
| `/lib/memory/profile/**` | Mem0 |
| `/lib/memory/kg/query_subgraph` | 图子图查询 | stable |
| `/lib/memory/kg/write_subgraph` | 图子图写入 | stable |
> 执行器必须预加载并校验以上子图。社区可扩展，但不得修改其 `signature`。



### 附录 D：记忆原语演进路线

- 6 个核心子图（`set`, `get_latest`,  `store`, `recall`, `update`, `get`）
- 实验性：
  - `/lib/memory/orchestrator/hybrid_recall@v1`（融合结构化+语义）
  - 支持记忆 TTL（`assign` + `$.now` + 过期策略）


### 附录 E：版本兼容性策略

- **小版本（3.x）**：向后兼容，仅增功能  
- **大版本（x.0）**：可能不兼容，提供迁移工具  
- **签名变更**：`stable` 子图仅可增加字段，不可删除/修改类型  
- **执行器**：支持多版本共存，路径版本后缀优先  

### 附录 F：适配层参考实现指南（新增）

> **注意**：本附录仅提供参考实现模式，不强制要求。执行器可自由选择实现细节，只要符合接口契约。

#### F.1 适配层架构

```
+---------------------+
|  /lib/memory/kg/**  |  ← 标准接口层（规范定义）
+---------------------+
          ↓
+---------------------+
|  执行器适配层        |  ← 将标准接口映射到具体工具
+---------------------+
          ↓
+---------------------+
|  注册工具           |  ← 具体后端实现（GFM-RAG/Neo4j等）
+---------------------+
```

---

> AgenticDSL v3.6 是迈向 **AI 原生操作系统** 的坚实一步。  
> 通过 **三层抽象 + 对话协议标准化 + Core SDK 契约化**，  
> 为构建 **可靠、可协作、可进化的智能体生态** 提供工业级工程基石。

**发布计划**：2025 Q4 开源参考执行器 + 对话子图参考实现 
