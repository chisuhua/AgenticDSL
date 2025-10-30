### AgenticDSL `/__meta__`
```yaml
version: "3.1"
mode: dev
execution_budget:
  max_nodes: 10
  max_llm_calls: 1
```

### AgenticDSL `/main/query`
```yaml
type: tool_call
tool: custom_db_query
arguments:
  table: "users"
  filter: "active=true"
output_keys: ["db_result"]
next: "/main/analyze"
```

### AgenticDSL `/main/analyze`
```yaml
type: assign
assign:
  summary: "Found {{ $.db_result.rows }} active users"
next: "/end"
```

### AgenticDSL `/end`
```yaml
type: end
```

