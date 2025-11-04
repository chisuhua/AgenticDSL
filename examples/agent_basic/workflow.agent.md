### AgenticDSL `/__meta__`
```yaml
# --- BEGIN AgenticDSL ---
version: "3.1"
mode: dev
entry_point: "/main/start"
execution_budget:
  max_nodes: 10
  max_llm_calls: 1
# --- END AgenticDSL ---
```

### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
entry: start
nodes:
  - id: start
    type: start
    next: [/main/query]
  - id: end
    type: end
# --- END AgenticDSL ---
```
### AgenticDSL `/main/query`
```yaml
# --- BEGIN AgenticDSL ---
type: tool_call
tool: custom_db_query
arguments:
  table: "users"
  filter: "active=true"
output_keys: ["db_result"]
next: "/main/analyze"
# --- END AgenticDSL ---
```

### AgenticDSL `/main/analyze`
```yaml
# --- BEGIN AgenticDSL ---
type: assign
assign:
  summary: "Found {{ $.db_result.rows }} active users"
next: "/end"
# --- END AgenticDSL ---
```

### AgenticDSL `/end`
```yaml
# --- BEGIN AgenticDSL ---
type: end
# --- END AgenticDSL ---
```

