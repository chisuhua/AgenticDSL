### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
entry: start
nodes:
  - id: start
    type: start
    next: [prepare]
  - id: prepare
    type: assign
    assign:
      num1: "15"
      num2: "27"
    next: [compute]
  - id: compute
    type: tool_call
    tool: calculate
    arguments:
      a: "{{ num1 }}"
      b: "{{ num2 }}"
      op: "+"
    output_keys: "result"
    next: [ask_llm]
  - id: ask_llm
    type: llm_call
    prompt_template: "The calculation result is {{ result.result }}. Please explain it."
    output_keys: "explanation"
    next: [end]
  - id: end
    type: end
# --- END AgenticDSL ---
```
