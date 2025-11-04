# AgenticDSL v3.2 内存扩展提案（Memory Extension Proposal, MEP-001）

**提案编号**：MEP-001  
**规范版本**：AgenticDSL v3.2（草案）  
**提交日期**：2025年10月31日  
**作者**：AgenticDSL Working Group – Memory SIG  
**许可协议**：CC BY-SA 4.0  
**状态**：草案（Draft）  
**目标**：为 AgenticDSL 生态提供标准化、契约化、可复用的混合记忆调用接口  

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

```markdown
### AgenticDSL `/lib/memory/state/set@v1`
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

### AgenticDSL `/lib/memory/state/get_latest@v1`
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

``` ```

#### 12.1.2 时间知识图谱操作（中期+长期）

> 注：实际存储由外部系统（如 Graphiti）实现，本子图仅封装调用。

```markdown
### AgenticDSL `/lib/memory/kg/write_fact@v1`
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

### AgenticDSL `/lib/memory/kg/query_latest@v1`
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

``` ```

---

#### 12.1.3 语义记忆操作（长期记忆）

```markdown
### AgenticDSL `/lib/memory/vector/store@v1`
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

### AgenticDSL `/lib/memory/vector/recall@v1`
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

``` ```

---

#### 11.1.4 用户画像管理（长期记忆）

```markdown
### AgenticDSL `/lib/memory/profile/update@v1`
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

### AgenticDSL `/lib/memory/profile/get@v1`
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

``` ```


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

```yaml
### AgenticDSL '/main/booking'
type: assign
assign:
  expr: "2025-11-20"
  path: "user_input.date"
next: "/lib/memory/state/set@v1?key=travel.departure_date&value={{ $.user_input.date }}"

### AgenticDSL '/main/confirm'
type: assign
assign:
  expr: "已记录您的出发日期为 {{ $.memory.state.travel.departure_date }}"
  path: "response.text"
next: "/end"
```

> ✅ 应用层无需关心记忆后端，仅依赖标准接口。

---

## 八、演进路线

- **v3.2**：纳入上述 6 个核心子图（`set`, `get_latest`, `write_fact`, `query_latest`, `store`, `recall`, `update`, `get`）
- **v3.3**（实验性）：
  - `/lib/memory/orchestrator/hybrid_recall@v1`（融合结构化+语义）
  - 支持记忆 TTL（`assign` + `$.now` + 过期策略）

---

## 九、附录：与现有系统的映射

| AgenticDSL 接口 | 推荐后端实现 |
|----------------|------------|
| `/lib/memory/state/**` | Context（内存） |
| `/lib/memory/kg/**` | Graphiti（首选）、Cognee |
| `/lib/memory/vector/**` | LightRAG + Qdrant/FAISS |
| `/lib/memory/profile/**` | Mem0 |

---

## 十、结论

本提案通过 **标准化记忆调用语义**，使 AgenticDSL 应用能够：
- **安全地** 使用混合记忆；
- **无需重复造轮子**；
- **无缝切换记忆后端**；
- **支持 LLM 自动生成记忆逻辑**。

建议将本 MEP-001 纳入 **AgenticDSL v3.2 规范附录 D：Core Memory SDK**。

---

> **AgenticDSL 不应定义“如何存储记忆”，而应定义“如何调用记忆”**。  
> 本提案正是这一哲学的实践。

© 2025 AgenticDSL Working Group. All rights reserved.  
欢迎社区评审与共建：https://github.com/agentic-dsl/spec/pulls
