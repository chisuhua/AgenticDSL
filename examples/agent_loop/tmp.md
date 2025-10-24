以下是为 **阶段 3 的闭环测试** 量身定制的两个文件：

---

## ✅ 1. `initial.md`（主工作流，包含 `/main` 和 `/__meta__`）

```markdown
```

> ✅ 特点：
> - 包含 `/__meta__` 预算控制（阶段 2）
> - `/main/llm_step` 会暂停，触发 LLM 生成
> - 生成内容将通过 `continue_with_generated_dsl` 合并执行

---

## ✅ 2. 标准库 DSL 示例：`lib/math/add.md`

> 放置于 `./lib/math/add.md`（供 `StandardLibraryLoader::load_from_directory` 加载）

```markdown
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
```

> ✅ 特点：
> - 路径为 `/lib/math/add` → 自动识别为标准库
> - 声明 `signature` 和空 `permissions`
> - 使用内置 `calculate` 工具
> - 以 `/end_soft` 结束（软终止，不中断主流程）

---

## ✅ 3. 可选：`lib/utils/noop.md`（空操作）

```markdown
### AgenticDSL `/lib/utils/noop`
```yaml
# --- BEGIN AgenticDSL ---
type: end
metadata:
  termination_mode: "soft"
# --- END AgenticDSL ---
```
```

> 已在 `system_nodes.cpp` 中预注册，此文件可省略。

---

## 🔧 使用说明

1. **目录结构建议**：
   ```
   your_project/
   ├── initial.md
   └── lib/
       └── math/
           └── add.md
   ```

2. **在 `StandardLibraryLoader::load_builtin_libraries()` 中注册路径**（或启用目录扫描）：
   ```cpp
   // 在 loader.cpp 中取消注释：
   // loader.load_from_directory("./lib");
   ```

3. **LLM Prompt 中将自动出现**：
   ```json
   "available_subgraphs": [
     {
       "path": "/lib/math/add",
       "signature": "(a: number, b: number) -> sum: number",
       "permissions": [],
       "is_subgraph": true
     }
   ]
   ```

4. **LLM 可生成如下 DSL**：
   ```markdown
   ### AgenticDSL `/main/call_add`
   ```yaml
   # --- BEGIN AgenticDSL ---
   type: assign
   assign:
     inputs: '{"a": 15, "b": 27}'
   next: ["/lib/math/add"]
   # --- END AgenticDSL ---
   ```
   ```

> 💡 注意：`next: ["/lib/math/add"]` 会跳转到标准库子图，执行完后因 `/end_soft` 返回主图继续。

---

如需更多标准库示例（如 `web_search`、`weather`），也可继续提供！

