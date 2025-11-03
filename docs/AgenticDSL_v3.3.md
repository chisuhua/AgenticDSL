# AgenticDSL 规范 v3.3
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
| 2. 内存记忆和推理原语层   | 规范提供的稳定接口实现         | 路径：`/lib/memory/**`, `/lib/reasoning/**`，版本稳定 |
| 3. 知识应用层             | 用户/社区扩展的领域逻辑        | 路径：`/lib/workflow/**`, `/lib/knowledge/**`，需带 `signature` |

✅ **所有复杂逻辑必须通过子图组合实现，禁止在叶子节点中编码高层语义。**

### 2.1 层间契约规则
- **执行 → 推理/记忆**：仅通过上下文传递数据，禁止直接 API 调用  
- **推理/记忆 → 知识应用**：必须通过 `signature` 暴露能力  
- **禁止跨层跳转**：知识应用层不得直接调用执行原语层（必须通过 `/lib/**` 封装）

### 2.2 适配器模式显式化  
所有外部系统交互必须通过规范定义的工具接口：
- **工具注册表**：执行器维护 `tool_schema`，声明输入/输出契约  
- **适配器隔离**：DAG 仅通过 `tool_call` 与工具交互，不依赖实现细节  
- **示例**：`kg_write_fact` 封装图数据库实现，DAG 仅关心接口  

---

## 三、术语表

| 术语             | 定义                                                                 |
|------------------|----------------------------------------------------------------------|
| 子图（Subgraph） | 以 `### AgenticDSL '/path'` 开头的逻辑单元                           |
| 动态生长         | 通过 `generate_subgraph` 在运行时生成新子图并注册到 `/dynamic/**`     |
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

### 5.7 `generate_subgraph`（安全增强）

- **关键约束**：
  - LLM 必须输出 `### AgenticDSL '/dynamic/...'` 块  
  - **不得用于调用已有子图或生成自然语言**  
  - `output_path` 必须为 `/dynamic/...`，**禁止写入 `/lib/**`**  
  - LLM 调用必须包含 `seed` 与 `temperature`  
  - 输出必须通过 JSON Schema 验证；失败则跳转至 `/self/repair`

### 5.8 `start` / `resource`
- `resource`：声明式依赖检查（凭据、网络、权限），调度前验证

---

## 六、统一文档结构

### 6.1 路径命名空间（关键强化）

- `/lib/**`：**只读命名空间**，禁止任何运行时写入或覆盖（包括 `generate_subgraph`）  
  - `/lib/reasoning/**`：推理原语  
  - `/lib/memory/**`：记忆原语  
  - `/lib/workflow/**`、`/lib/knowledge/**`：应用层  
- `/dynamic/**`：运行时生成（唯一允许写入的非主流程空间）  
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

### 6.3 LLM 意图结构化  
`<!-- LLM_INTENT: {"task": "..."} -->` → 执行器解析为 JSON 并记录到 Trace

### 6.4 显式执行入口

为确保执行起点明确、避免对图结构（如入度）的隐式依赖，每个 `.agent.md` 文件**必须**在 `/__meta__` 块中声明 `entry_point` 字段。

```yaml
AgenticDSL `/__meta__`
version: "3.3"
mode: dev
entry_point: "/main/start"  # ✅ 必需：DAG 执行入口路径
execution_budget:
  max_nodes: 20
  max_subgraph_depth: 2
  max_duration_sec: 30
```

#### 规则说明：
- 必需性：若未声明 `entry_point`，执行器必须拒绝启动并返回错误 `ERR_MISSING_ENTRY_POINT`。
- 路径要求：`entry_point` 值必须为**绝对路径**（以 `/` 开头），且指向文档中**已定义的子图节点**。
- 唯一性：每个 `.agent.md` 仅允许一个 `entry_point`。
- 与动态生成兼容：`entry_point` 可指向 `/dynamic/**` 节点（例如恢复中断任务），但该节点必须在执行前已注册。
- 开发体验：推荐将入口设为 `/main/start`（类型为 `start` 或 `assign`），避免直接从复杂逻辑开始。

#### 执行器行为：
1. 启动时解析 `/__meta__` 块；
2. 验证 `entry_point` 路径存在；
3. 将该节点作为初始调度节点入队；
4. **不再计算图入度或扫描“无前驱节点”**。

> **设计动机**：动态 DAG（如含 `generate_subgraph`）可能在运行时改变拓扑，静态入度分析失效。显式入口是构建可复现、可中断、可恢复智能体工作流的基础。

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

### 7.3 可观测性（Trace Schema）  
兼容 OpenTelemetry，包含：执行状态、上下文变更、输出匹配、LLM 意图、预算快照等

### 7.4 标准库版本与依赖管理  
- 路径支持语义化版本：`/lib/...@v1`  
- 子图可声明依赖：`requires: - lib: "/lib/reasoning/...@^1.0"`  
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


### 8.5 开发模式  
- `dev`：`signature_validation: warn`，允许 `last_write_wins`，含上下文快照  
- `prod`（默认）：强制 `strict`，禁用 `last_write_wins`，最小权限沙箱启用  

### 8.6 Trace 增强  
`dev` 模式下包含快照信息（若 budget 允许）

---

## 九、LLM 生成指令（System Prompt）

你是一个**推理与行动架构师**。必须：
- 输出一个或多个 `### AgenticDSL '/path'` 块  
- **不要输出自然语言解释**（除非在 `<!-- LLM: ... -->` 中）  
- 遵守预算：递归深度 ≤ `{{ budget.subgraph_depth_left }}`  
- 优先调用标准库（清单含 `signature`）  
- **所有 LLM 调用必须包含 `seed` 与 `temperature`**

---

## 十、Context 快照机制规范（安全增强）

### 10.4 快照恢复方式（安全限制）

```yaml
type: assign
assign:
  expr: "{{ $.ctx_snapshots['/main/step3'] }}"  # ✅ 静态键
  path: ""
```

⚠️ **安全限制**：`$.ctx_snapshots` 的访问键**必须为静态字符串**，禁止动态计算（如 `{{ $.key }}`），防止信息泄露

---

## 十一、推理原语层

### 11.1 `/lib/reasoning/with_rollback`  
自动触发快照 → 执行尝试路径 → 失败则恢复上下文 → 执行 fallback

### 11.2 `/lib/reasoning/hypothesis_branch`  
- `fork` 多路径  
- 仅允许一个成功（`error_on_conflict`）  
- 失败时回滚至 `fork` 前状态

---

## 十二、内存记忆原语

### 12.1 核心接口标准化

- **统一参数模式**：`key`/`value` 或 `head`/`relation`/`tail`  
- **自动附加元数据**：`timestamp`（ISO8601）、`source_node`、`user_id`  
- **错误处理标准化**：返回统一错误码（如 `ERR_MEMORY_NOT_FOUND`）  
- **修正签名格式**：移除空格，确保严格 JSON Schema 合规

---

## 十三、完整示例

### 13.1 基础示例

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

### 13.2 完整错误处理示例

```yaml
### AgenticDSL `/main/equation_solver`
type: generate_subgraph
prompt_template: "生成求解 {{ $.expr }} 的DAG子图..."
next: "/lib/reasoning/with_rollback@v1"
on_failure: "/main/equation_solver/fallback"

### AgenticDSL `/main/equation_solver/fallback`
type: assign
assign:
  expr: "{{ $.ctx_snapshots['/main/equation_solver'] }}"
  path: ""
next: "/lib/human/clarify_intent@v1"
```

### 13.3 记忆+推理组合示例

```yaml
AgenticDSL `/main/learn_from_failure`
type: assign
assign:
  expr: "方程 {{ $.expr }} 求解失败：{{ $.error }}"
next: "/lib/memory/vector/store@v1"
```

---

## 附录

### 附录 A：最佳实践与约定

**A1. 时间上下文约定（非强制）**
- `$.now`: ISO8601 当前时间（由执行器注入）
- `$.time_anchor`: 任务参考时间点
- `$.timeline[]`: `{ts: "...", event: "...", source: "..."}`

**A2. 禁止行为清单 **
- 在 DAG 内实现异步回调
- 在叶子节点中编码高层推理逻辑
- 使用 `generate_subgraph` 调用已有子图
- 输出非 `### AgenticDSL` 块的 LLM 内容
- 在生产模式下使用 `last_write_wins` 合并策略

**A3. 推荐开发工作流 **：
1. `agentic validate example.agent.md`  
2. `agentic simulate --mode=dev`  
3. 从 Trace 提取失败案例，更新 `expected_output`  
4. 通过 `archive_to` 沉淀验证通过模块  
5. **生产部署必须显式设置 `mode: prod`**

**A4. 性能边界指南（新增）**：
- 上下文大小：<1MB（>512KB 启用快照压缩）  
- 单子图节点数：<50  
- 预算配置：`max_nodes: 10 × [预期分支数]`，`max_subgraph_depth: 3`  

### 附录 B：`expected_output` 与 `signature.outputs` 分工

| 机制 | 作用域 | 用途 | 校验时机 |
|------|--------|------|--------|
| `signature.outputs` | **子图接口** | 契约：调用者与被调用者约定 | 调用前（输入）、调用后（输出） |
| `expected_output` | **单次执行** | 验证：本次任务期望的具体值 | 执行后（Trace 记录，可选告警） |

### 附录 C：核心标准库清单

| 路径 | 用途 | 稳定性 |
|------|------|--------|
| `/lib/reasoning/assert` | 中间结论验证 | stable |
| `/lib/human/clarify_intent` | 请求用户澄清意图 | stable |
| `/lib/human/approval` | 人工审批节点 | stable |
| `/lib/workflow/parallel_map` | 基于 `fork` 的 map 封装 | experimental |
| `/lib/reasoning/solve_quadratic` | 二次方程求解示例 | experimental |

> 执行器必须预加载并校验以上子图。社区可扩展，但不得修改其 `signature`。

- `/lib/reasoning/solve_quadratic`：保留为 `experimental`，**但主示例不依赖**

### 附录 D：记忆原语演进路线

- 6 个核心子图（`set`, `get_latest`, `write_fact`, `query_latest`, `store`, `recall`, `update`, `get`）
- 实验性：
  - `/lib/memory/orchestrator/hybrid_recall@v1`（融合结构化+语义）
  - 支持记忆 TTL（`assign` + `$.now` + 过期策略）


**与现有系统的映射**

| AgenticDSL 接口 | 推荐后端实现 |
|----------------|------------|
| `/lib/memory/state/**` | Context（内存） |
| `/lib/memory/kg/**` | Graphiti（首选）、Cognee |
| `/lib/memory/vector/**` | LightRAG + Qdrant/FAISS |
| `/lib/memory/profile/**` | Mem0 |


### 附录 E：版本兼容性策略

- **小版本（3.x）**：向后兼容，仅增功能  
- **大版本（x.0）**：可能不兼容，提供迁移工具  
- **签名变更**：`stable` 子图仅可增加字段，不可删除/修改类型  
- **执行器**：支持多版本共存，路径版本后缀优先  

---

> AgenticDSL v3.3 是迈向 **AI 原生操作系统** 的坚实一步。  
> 通过 **三层抽象 + 契约驱动 + 确定性治理 + 开发者友好设计**，  
> 为构建 **可靠、可协作、可进化的智能体生态** 提供工业级工程基石。
