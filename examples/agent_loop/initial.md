### AgenticDSL `/__meta__`
```yaml
# --- BEGIN AgenticDSL ---
execution_budget:
  max_nodes: 10
  max_llm_calls: 3
  max_duration_sec: 60
# --- END AgenticDSL ---
```

### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/llm_step"]

  - id: llm_step
    type: llm_call
    prompt_template: |
      User request: {{ user_input }}
      Generate the next step using available tools or standard libraries.
    output_keys: ["generated_dsl"]
    next: ["/main/end"]
  
  - id: end
    type: end
# --- END AgenticDSL ---
```

## 特点：
- 包含 `/__meta__` 预算控制（阶段 2）
- `/main/llm_step` 会暂停，触发 LLM 生成
- 生成内容将通过 `continue_with_generated_dsl` 合并执行


