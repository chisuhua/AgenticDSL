
## 示例

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


