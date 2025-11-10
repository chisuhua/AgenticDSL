
# 标准原语

## 附录 C：核心标准库清单

| 路径 | 用途 | 稳定性 |
|------|------|--------|
| `/lib/dslgraph/generate@v1` | 安全生成动态子图 | stable |
| `/lib/reasoning/assert` | 中间结论验证 | stable |
| `/lib/reasoning/hypothesize_and_verify` | 多假设验证 | stable |
| `/lib/reasoning/try_catch` | 异常回溯 | stable |
| `/lib/reasoning/stepwise_assert` | 分步断言 | stable |
| `/lib/reasoning/graph_guided_hypothesize` | 图引导假设生成 | experimental |
  `/lib/reasoning/counterfactual_compare@v1`|反事实推理，对比多场景|experimental|
  `/lib/reasoning/induce_and_archive@v1`|从成功 Trace 归纳规则并归档|stable |
  `/lib/reasoning/iper_loop@v1` | “意图-计划-执行-反思”（IPER）闭环，用于鲁棒任务执行。||
| /lib/reasoning/generate_text@v1 | 基础生成 | stable |
| /lib/reasoning/structured_generate@v1 | 结构化输出 | stable |
| /lib/reasoning/continue_from_kv@v1 | KV 复用 | stable |
| /lib/reasoning/stream_until@v1 | 流式终止 | stable |
| /lib/reasoning/speculative_decode@v1 | 推测解码 | experimental |
| /lib/reasoning/fallback_text@v1** | 文本降级 | stable |
  `/lib/reasoning/iper_loop@v1`
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
  - reasoning: llm_generate
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
#### 10.2.8：Agentic-native 推理原语

> 以下 5 个子图构成 **推理能力契约基础**，执行器必须实现。

##### 1. `/lib/reasoning/generate_text@v1`（stable）
```yaml
signature:
  inputs:
    - name: prompt; type: string; required: true
    - name: model; type: string; required: true
    - name: seed; type: integer; required: true
    - name: temperature; type: number; default: 0.0
    - name: max_tokens; type: integer; default: 256
  outputs:
    - name: text; type: string; required: true
    - name: kv_handle; type: string; required: false
version: "1.0"
stability: stable
requires:
  - tool: "native_inference_core"
permissions:
  - reasoning: llm_generate
on_error: "/lib/reasoning/fallback_text@v1"
type: llm_call
llm:
  model: "{{ $.model }}"
  seed: "{{ $.seed }}"
  temperature: "{{ $.temperature }}"
  max_tokens: "{{ $.max_tokens }}"
  prompt: "{{ $.prompt }}"
```

##### 2. `/lib/reasoning/structured_generate@v1`（stable）
```yaml
signature:
  inputs:
    - name: prompt; type: string; required: true
    - name: model; type: string; required: true
    - name: seed; type: integer; required: true
    - name: output_schema; type: object; required: true
  outputs:
    - name: parsed_output; type: object; required: true
version: "1.0"
stability: stable
requires:
  - lib: "/lib/reasoning/generate_text@^1.0"
  - tool: "native_inference_core"
permissions:
  - reasoning: structured_generate
on_error: "/lib/reasoning/fallback_structured@v1"
type: llm_call
llm:
  model: "{{ $.model }}"
  seed: "{{ $.seed }}"
  temperature: 0.0
  prompt: "{{ $.prompt }}"
  output_schema: "{{ $.output_schema }}"
```

##### 3. `/lib/reasoning/continue_from_kv@v1`（stable）
```yaml
signature:
  inputs:
    - name: kv_handle; type: string; required: true
    - name: new_prompt; type: string; required: true
    - name: model; type: string; required: true
    - name: max_tokens; type: integer; default: 256
  outputs:
    - name: continuation; type: string; required: true
    - name: updated_kv_handle; type: string; required: false
version: "1.0"
stability: stable
requires:
  - tool: "native_inference_core"
permissions:
  - reasoning: llm_generate
on_error: "/lib/reasoning/fallback_text@v1"
type: llm_call
llm:
  model: "{{ $.model }}"
  prompt: "{{ $.new_prompt }}"
  kv_handle: "{{ $.kv_handle }}"
  max_tokens: "{{ $.max_tokens }}"
```

##### 4. `/lib/reasoning/stream_until@v1`（stable）
```yaml
signature:
  inputs:
    - name: prompt; type: string; required: true
    - name: model; type: string; required: true
    - name: seed; type: integer; required: true
    - name: stop_condition; type: string; required: true
    - name: max_tokens; type: integer; default: 2048
  outputs:
    - name: streamed_output; type: string; required: true
version: "1.0"
stability: stable
requires:
  - tool: "native_inference_core"
permissions:
  - reasoning: stream_output
on_error: "/lib/reasoning/fallback_text@v1"
type: llm_call
llm:
  model: "{{ $.model }}"
  seed: "{{ $.seed }}"
  prompt: "{{ $.prompt }}"
  stop_condition: "{{ $.stop_condition }}"
  max_tokens: "{{ $.max_tokens }}"
```

##### 5. `/lib/reasoning/speculative_decode@v1`（experimental）
```yaml
signature:
  inputs:
    - name: prompt; type: string; required: true
    - name: target_model; type: string; required: true
    - name: draft_model; type: string; default: "phi-3-mini"
    - name: max_speculative_tokens; type: integer; default: 5
  outputs:
    - name: verified_output; type: string; required: true
    - name: acceptance_rate; type: number; required: true
version: "1.0"
stability: experimental
requires:
  - tool: "native_inference_core"
permissions:
  - reasoning: speculative_decode
on_error: "/lib/reasoning/fallback_text@v1"
type: llm_call
llm:
  model: "{{ $.target_model }}"
  draft_model: "{{ $.draft_model }}"
  prompt: "{{ $.prompt }}"
  max_speculative_tokens: "{{ $.max_speculative_tokens }}"
```

> 所有子图均遵守：
> - 路径命名规范（6.1）
> - 签名契约（6.2）
> - 权限最小化（7.2）
> - 错误处理（`on_error`）

#### 新增 fallback 子图（配套必需）
##### `/lib/reasoning/fallback_text@v1`（stable）
```yaml
signature:
  inputs:
    - name: error_context; type: object; required: true
  outputs:
    - name: text; type: string; required: true
version: "1.0"
stability: stable
type: assign
assign:
  expr: "推理失败：{{ $.error_context.message | default('未知错误') }}"
  path: "result.text"
```

##### `/lib/reasoning/fallback_structured@v1`（stable）
```yaml
signature:
  inputs:
    - name: error_context; type: object; required: true
  outputs:
    - name: parsed_output; type: object; required: true
version: "1.0"
stability: stable
type: assign
assign:
  expr: "{}"
  path: "result.parsed_output"
```  
---

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

#### 10.6.2 工具注册要求  
- 新增工具：`native_inference_core`
  - 输入：`llm` 对象（含上述字段）
  - 输出：`{ text, kv_handle?, parsed_output? }`
  - 能力：`tokenize, kv_alloc, model_step, compile_grammar, stream_until`


