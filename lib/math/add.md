### AgenticDSL `/lib/math/add`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
signature: "(a: number, b: number) -> sum: number"
permissions: []
nodes:
  - id: add_tool
    type: tool_call
    tool: calculate
    arguments:
      a: "{{ inputs.a }}"
      b: "{{ inputs.b }}"
      op: "+"
    output_keys: ["result"]
    next: ["/lib/math/add/result_assign"]

  - id: result_assign
    type: assign
    assign:
      sum: "{{ result.result }}"
    output_keys: ["sum"]
    next: ["/end_soft"]
# --- END AgenticDSL ---
```

## 特点：
- 路径为 `/lib/math/add` → 自动识别为标准库
- 声明 `signature` 和空 `permissions`
- 使用内置 `calculate` 工具
- 以 `/end_soft` 结束（软终止，不中断主流程）
