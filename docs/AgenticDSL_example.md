# AgenticDSL 示例集（v3.10 参考实现版）

> 本文档使用当前参考执行器 v1.0 支持的 DSL 语法。  
> 完整子图格式使用 `graph_type: subgraph` + `nodes:` 数组；单节点也可用独立 `### AgenticDSL '/path'` 块定义。

## 12.0 完整可运行示例（推荐格式）

本示例展示参考执行器实际支持的完整 DSL 写法，包含主图、工具调用、断言和 LLM 生成。

#### AgenticDSL `/__meta__`
```yaml
# --- BEGIN AgenticDSL ---
version: "3.10"
mode: dev
entry_point: "/main/start"
execution_budget:
  max_nodes: 20
  max_subgraph_depth: 2
  max_duration_sec: 60
# --- END AgenticDSL ---
```

#### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/prepare"]

  - id: prepare
    type: assign
    assign:
      expr: "x^2 + 2x + 1 = 0"
      user_name: "Alice"
    next: ["/main/greet"]

  - id: greet
    type: dsl_call
    prompt_template: "你好，{{ user_name }}！请帮我分析方程 {{ expr }}"
    llm_tool_name: "llama-default"
    llm_params:
      temperature: 0.5
      max_tokens: 256
    output_keys: ["llm_response"]
    next: ["/main/compute"]

  - id: compute
    type: tool_call
    tool: calculate
    arguments:
      a: "1"
      b: "2"
      op: "+"
    output_keys: ["sum_result"]
    next: ["/main/verify"]

  - id: verify
    type: assert
    condition: "{{ sum_result.result == 3 }}"
    on_failure: "/main/error"
    next: ["/main/end"]

  - id: error
    type: assign
    assign:
      error_msg: "计算结果异常：{{ sum_result }}"
    next: ["/main/end"]

  - id: end
    type: end
    metadata:
      termination_mode: hard
# --- END AgenticDSL ---
```

**对应的 C++ 宿主代码**：
```cpp
auto engine = DSLEngine::from_markdown(dsl_content);

// 注册普通工具
engine->register_tool("calculate",
    [](const std::unordered_map<std::string, std::string>& args) -> nlohmann::json {
        double a = std::stod(args.at("a"));
        double b = std::stod(args.at("b"));
        return {{"result", a + b}};
    });

// 注册 LLM 工具（使用 LlamaTool）
auto llm = std::make_unique<LlamaTool>(LlamaAdapter::Config{
    .model_path = "/models/llama-2-7b.gguf"
});
engine->register_llm_tool("llama-default", std::move(llm));

auto result = engine->run({});
```

---


## 示例

### 12.1 基础对话示例

#### AgenticDSL `/__meta__`
```yaml
version: "3.10"
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
# --- BEGIN AgenticDSL ---
type: assign
assign:
  intent: "我想订机票"
next: "/lib/conversation/start_topic@v1"
# --- END AgenticDSL ---
```

#### AgenticDSL `/topics/booking/main`
```yaml
# --- BEGIN AgenticDSL ---
type: generate_subgraph
prompt_template: "请询问出发地和目的地..."
output_keys: ["booking_graph_path"]
next: "/lib/memory/state/set@v1"
# --- END AgenticDSL ---
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
# --- BEGIN AgenticDSL ---
type: generate_subgraph
prompt_template: "方程 {{ $.expr }} 求解失败。请重写为标准形式并生成新 DAG。"
output_keys: ["repair_graph_path"]
signature_validation: warn
on_signature_violation: "/self/fallback"
next: "/dynamic/repair_123"
# --- END AgenticDSL ---
```


