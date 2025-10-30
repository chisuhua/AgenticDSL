## 📝 示例工作流 (`workflow.agent.md`)

```markdown
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
```

---

## 📁 项目结构

```
AgenticDSL/
├── CMakeLists.txt
├── main.cpp                 # ← 应用入口
├── workflow.agent.md        # ← 示例工作流
└── execution_trace.json     # ← 自动生成的 Trace 文件
```

---

## ✅ 优势

- **Trace 完整**：包含上下文 delta、预算快照、快照键
- **工具灵活**：运行时注册自定义工具
- **安全隔离**：工具通过 `ToolRegistry` 权限检查
- **符合规范**：完全基于 v3.1 的 `ExecutionSession` + `TraceExporter`

此设计可直接用于生产环境或调试分析。是否需要 **CMakeLists.txt 完整示例**？
