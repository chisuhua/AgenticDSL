# AgenticDSL 规范 v3.8  
**安全 · 可终止 · 可调试 · 可复用 · 可契约 · 可验证**  
*“优秀的 DSL 不是让机器更容易执行，而是让人类更容易表达意图，同时让机器能够可靠地验证这种意图。”*  
— AgenticDSL 核心哲学  

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


### 5.9 `llm_call`  
**语义**：调用推理引擎内置的 LLM 推理内核，用于生成文本、结构化输出、流式终止等高级推理行为。  
**仅可通过 `/lib/reasoning/**` 子图调用，禁止用户在知识应用层或主 DAG 中直接使用。**

#### 字段定义
| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| llm.model | string | ✅ | 模型标识（如 `gpt-4o`, `llama-3-8b`） |
| llm.seed | integer | ✅ | 确定性种子 |
| llm.temperature | number | ✅ | 温度（0.0–1.0） |
| llm.prompt | string | ✅ | 提示词（Inja 渲染后） |

#### 标准可选字段（执行器必须识别，未声明则忽略）
| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| llm.max_tokens | integer | 256 | 最大生成长度 |
| llm.output_schema | object | — | JSON Schema，用于结构化输出约束 |
| llm.kv_handle | string | — | KV 缓存句柄，用于续写 |
| llm.stop_condition | string | — | 流式终止条件（如特殊 token 或字符串） |
| llm.draft_model | string | `"phi-3-mini"` | 推测解码头模型 |
| llm.max_speculative_tokens | integer | 5 | 推测最大 token 数 |

#### 行为规则
- 所有字段必须通过 `Inja` 安全渲染（禁止任意代码）
- 若字段未定义（如 `output_schema`），执行器应忽略而非报错
- 返回值必须包含 `text` 字段；若支持 KV 复用，可附加 `kv_handle`
- **Trace 必须记录**：
  ```json
  {
    "llm_call": {
      "model": "gpt-4o",
      "prompt_tokens": 120,
      "completion_tokens": 80,
      "used_features": ["output_schema", "kv_handle"],
      "backend_used": "AgenticInfer-v1.4"
    }
  }
  ```

#### 权限要求
- 必须声明对应推理权限（如 `reasoning: llm_generate`）
- 未授权 → 跳转 `on_error`（由调用子图定义）

> **注**：`llm_generate_dsl`（5.7）与 `llm_call`（本节）分工明确：
> - `llm_generate_dsl`：仅用于生成 AgenticDSL 子图（动态 DAG）
> - `llm_call`：用于通用推理行为（文本、结构化等），语义更广

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
  - type: reasoning
    capabilities:
      - text_generation
      - structured_generate
      - kv_continuation
      - stream_output
      - speculative_decode
  - type: tool
    name: native_inference_core
    scope: internal
    capabilities: [tokenize, kv_alloc, model_step, compile_grammar, stream_until]
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

---

## 七、安全与工程保障  

### 7.1 标准库契约强制  
启动时预加载并校验所有 `/lib/**`；LLM 生成时 `available_subgraphs` 必须含 `signature`

### 7.2 权限与沙箱（补充）

```yaml
permissions:
  - tool: web_search → scope: read_only
  - runtime: python3 → allow_imports: [json, re]
  - network: outbound → domains: ["api.example.com"]
  - generate_subgraph: { max_depth: 2 }
```

**权限组合规则**：
- 交集原则：节点权限 ∩ 父上下文授权权限  
- 拒绝优先：任一缺失 → 跳转 `on_error`  
- 权限降级：子图调用时权限**只能减少**

> **资源声明是权限的前置契约**。执行器在启动时验证 `/__meta__/resources` 中声明的资源可用性后，才允许执行声明了对应 `permissions` 的节点。

推理权限类型：

| 权限 | 说明 | 最小权限范围 |
|------|------|-------------|
| `reasoning: llm_generate` | 基础文本生成 | 仅限 `llm_call` 调用 |
| `reasoning: structured_generate` | 结构化输出（需 `output_schema`） | 同上 |
| `reasoning: stream_output` | 流式终止（需 `stop_condition`） | 同上 |
| `reasoning: speculative_decode` | 推测解码 | 同上 |

> 权限组合遵循交集原则；未声明 → `on_error`

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

## 九、LLM 生成指令  

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
- `/lib/dslgraph/generate@v1`（stable）

### 10.2 推理原语（`/lib/reasoning/**`）
- `/lib/reasoning/hypothesize_and_verify@v1`
- `/lib/reasoning/stepwise_assert@v1`
- `/lib/reasoning/counterfactual_compare@v1`（experimental）
- `/lib/reasoning/try_catch@v1`
- `/lib/reasoning/induce_and_archive@v1`
- `/lib/reasoning/graph_guided_hypothesize@v1`（experimental）
- `/lib/reasoning/iper_loop@v1`
- `/lib/reasoning/generate_text@v1`
- `/lib/reasoning/structured_generate@v1`
- `/lib/reasoning/continue_from_kv@v1`
- `/lib/reasoning/stream_until@v1`
- `/lib/reasoning/speculative_decode@v1`（experimental）
- `/lib/reasoning/fallback_text@v1`
- `/lib/reasoning/fallback_structured@v1`

### 10.3 内存记忆原语（`/lib/memory/**`）
- `/lib/memory/state/set@v1`
- `/lib/memory/state/get_latest@v1`
- `/lib/memory/kg/query_subgraph@v1`
- `/lib/memory/kg/write_subgraph@v1`
- `/lib/memory/vector/store@v1`
- `/lib/memory/vector/recall@v1`
- `/lib/memory/profile/update@v1`
- `/lib/memory/profile/get@v1`

> **记忆更新权限**：
> - `/lib/memory/state/set@v1`：`memory: state_write`
> - `/lib/memory/kg/query_subgraph@v1`：`kg: subgraph_query`
> - `/lib/memory/kg/write_subgraph@v1`：`kg: subgraph_write`
> - `/lib/memory/vector/store@v1`：`vector: store`
> - `/lib/memory/vector/recall@v1`：`vector: recall`
> - `/lib/memory/profile/update@v1`：`profile: update`

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

### 10.4 对话原语语义（`/lib/conversation/**`）

支持下面功能：
- 多轮对话状态管理  
- 话题隔离与切换  
- 多角色上下文隔离  
- 会议协作与知识聚合  

原语列表:
- `/lib/conversation/start_topic@v1`（stable）
- `/lib/conversation/switch_role@v1`（stable）
- `/lib/conversation/meeting@v1`（stable）

> **对话上下文模型**：
> - 每个话题拥有独立上下文路径：`/topics/{topic_id}/context`
> - 角色切换更新 `conversation.current_role`
> - 会议协调器管理多角色状态同步
> - 通过 `/lib/memory/state` 存话题状态
> - 角色上下文隔离
> - 会议共享上下文 + 私有上下文

- **预算控制**：`max_conversation_turns`、`max_topics`、`max_roles`

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

### 10.5 工作流原语（`/lib/workflow/**`）
- `/lib/workflow/parallel_map@v1`（experimental）

### 10.6 世界模型及环境感知原语

### 10.7 资源工具

动态查询当前可用工具及其能力标签，供 LLM 规划使用。

- `/lib/tool/list_available@v1`

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



### 附录 D：记忆原语演进路线

- 6 个核心子图（`set`, `get_latest`,  `store`, `recall`, `update`, `get`）
- 实验性：
  - `/lib/memory/orchestrator/hybrid_recall@v1`（融合结构化+语义）
  - 支持记忆 TTL（`assign` + `$.now` + 过期策略）


---

> AgenticDSL v3.8 是AI-操作系统和推理能力标准化的关键一步。**  
> 通过 **三层抽象 + 对话协议标准化 + Core SDK 契约化**，  契约化推理原语 + 安全 `llm_call` + 资源声明联动，通过
> 为构建 **可靠、可协作、可进化的智能体生态** 提供工业级工程基石。

**发布计划**：2025 Q4 开源参考执行器 + 对话子图参考实现 
