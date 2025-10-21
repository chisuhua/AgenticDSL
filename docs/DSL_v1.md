以下是一份**面向 LLM 的 AgenticDSL 计算图语言描述规范文档**，专为通过提示词（prompt）引导 LLM 输出结构正确、语义清晰的计算图而设计。该规范强调**可解析性、一致性、子图支持与执行语义明确性**。


# 📜 AgenticDSL 计算图语言描述规范（LLM 输出版）

> 本规范用于指导大语言模型（LLM）生成符合 AgenticDSL 执行引擎要求的计算图描述。所有输出必须严格遵循以下格式与语义约定。

---

## 一、整体结构：以 Markdown 为载体

- 每个计算图节点或子图必须以 **三级 Markdown 标题** 开始：
  ```markdown
  ### AgenticDSL `<路径>`
  ```
  - `<路径>` 是该节点或子图的**全局唯一标识符**，格式为 `/命名空间/.../节点名`。
  - 路径必须以 `/` 开头，仅包含字母、数字、下划线 `_`、连字符 `-`，**禁止空格和特殊字符**。
  - 若路径以 `/` 结尾（如 `/auth/`），表示这是一个**子图容器**（包含多个节点和边）；否则表示单个节点。

- 路径下方必须紧跟一个 **YAML 代码块**，且内容必须包裹在以下边界注释之间：
  ```yaml
  # --- BEGIN AgenticDSL ---
  ...（YAML 内容）...
  # --- END AgenticDSL ---
  ```

> ✅ 正确示例：
> ```markdown
> ### AgenticDSL `/main/collect_user_intent`
> 
> ```yaml
> # --- BEGIN AgenticDSL ---
> type: llm_call
> prompt_template: "请理解用户意图：{{user_input}}"
> next: [route_intent]
> # --- END AgenticDSL ---
> ```

---

## 二、节点类型与字段定义

每个节点（非子图容器）必须声明 `type` 字段，取值如下：

| 类型 | 说明 | 必填字段 | 可选字段 |
|------|------|--------|--------|
| `start` | 起始节点，无操作，仅跳转 | `next` | `metadata` |
| `end` | 终止节点，返回当前上下文 | — | `output_keys`, `metadata` |
| `assign` | Inja 变量赋值 | `assign`（字典） | `next`, `metadata` |
| `tool_call` | 调用 MCP 工具 | `tool`, `arguments`（Inja 模板） | `next`, `retry`, `metadata` |
| `llm_call` | 调用 LLM | `prompt_template`（Inja 字符串） | `model`, `next`, `metadata` |
| `codelet_call` | 调用 codelet | `codelet`（路径，如 `/codelets/validate`） | `next`, `pass_context`, `metadata` |
| `dynamic_next` | 动态依赖节点 | `resolver`（codelet 路径） | `fallback_next`, `metadata` |

### 字段说明

- `next`: 静态后继节点 ID 或路径（字符串或列表）。仅在非动态流程中使用。
- `assign`: 字典，键为变量名（如 `user.name`），值为 Inja 表达式（如 `"{{input.split()[0]}}"`）。
- `tool`: 工具名称（字符串），需与 MCP 注册名一致。
- `arguments`: 传递给工具的参数模板，支持 Inja（如 `{"query": "{{search_term}}"}`）。
- `prompt_template`: LLM 提示模板，支持上下文变量（如 `"总结：{{history}}"`）。
- `codelet`: 引用的 codelet 路径（必须是有效的 AgenticDSL codelet 节点）。
- `resolver`: 在 `dynamic_next` 节点中，指定一个 codelet 路径，其返回值应为下一个节点路径列表。
- `output_keys`: （仅 `end` 节点）指定返回上下文中的哪些键作为最终输出。

---

## 三、子图（Subgraph）描述规范

当路径以 `/` 结尾（如 `/auth/`），表示该块定义一个**完整子图**，必须包含：

```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
entry: <入口节点ID>
exit: <出口节点ID>  # 可选，若多个出口可省略
nodes:
  - id: ...
    type: ...
    ...
edges:
  - from: <源节点ID>
    to: <目标节点ID>
# --- END AgenticDSL ---
```

- `nodes` 列表中的每个节点 **必须使用局部 ID**（不带路径前缀），如 `validate_token`。
- `edges` 显式定义依赖关系，优先于 `next` 字段（若同时存在，以 `edges` 为准）。
- 子图可被其他图通过 `codelet_call` 或未来扩展的 `subgraph_call` 引用（当前建议用 `codelet_call` 模拟）。

---

## 四、Codelet 节点规范

Codelet 是可执行代码单元，类型为 `codelet`（注意：不是 `codelet_call`）：

```markdown
### AgenticDSL `/codelets/validate_email`

```yaml
# --- BEGIN AgenticDSL ---
type: codelet
runtime: python3.10
code: |
  def main(context):
      email = context.get("user_email", "")
      if "@" in email and "." in email:
          return {"is_valid": True, "normalized": email.lower()}
      else:
          return {"is_valid": False}
# --- END AgenticDSL ---
```

- `runtime`: 指定执行环境（目前仅支持 `python3.10`）。
- `code`: 必须包含 `main(context)` 函数，接收上下文 dict，返回 dict。
- 返回值会 merge 到全局上下文中。

---

## 五、资源节点（可选）

资源作为特殊节点，类型为 `resource`：

```yaml
type: resource
resource_type: postgres | file | api_endpoint | custom
uri: "..."  # 或配置引用
scope: global | local
```

> 注：资源通常由执行引擎预加载，节点通过名称引用（如 `tool_call` 的 `arguments` 中使用 `{{resources.db_conn}}`）。

---

## 六、元数据（推荐）

所有节点或子图可包含 `metadata` 字段，用于文档、调试或追踪：

```yaml
metadata:
  description: "验证用户邮箱格式"
  author: "dev@example.com"
  version: "1.0"
  tags: ["validation", "user-onboarding"]
```

---

## 七、动态流程处理

- 静态流程：使用 `next` 字段直接指定后继。
- 动态流程：
  1. 当前节点 `next` 指向一个 `dynamic_next` 类型节点。
  2. 该 `dynamic_next` 节点必须指定 `resolver`（指向一个 codelet）。
  3. Codelet 执行后返回 `{"next": ["node_a", "node_b"]}`，引擎据此决定后续路径。

> ⚠️ 禁止在 `llm_call` 或 `tool_call` 中直接返回动态节点列表——必须通过 `dynamic_next` 中转。

---

## 八、输出要求（给 LLM 的指令）

当你（LLM）被要求生成 AgenticDSL 计算图时，请：

1. **始终使用 Markdown 三级标题 + YAML 代码块**；
2. **路径必须全局唯一且符合命名规范**；
3. **每个节点只做一件事，职责单一**；
4. **子图必须显式声明 `graph_type: subgraph` 并定义 `entry`**；
5. **不要省略 `# --- BEGIN/END AgenticDSL ---` 边界**；
6. **避免在 YAML 中使用复杂嵌套或自定义类型**，保持字段扁平、值为字符串/字典/列表；
7. **所有 Inja 表达式必须用双花括号 `{{}}` 包裹**。

---

## 附录：完整示例

```markdown
### AgenticDSL `/main`

```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
entry: start
nodes:
  - id: start
    type: start
    next: [ask_purpose]
  - id: ask_purpose
    type: llm_call
    prompt_template: "用户想做什么？输入：{{raw_input}}"
    next: [dispatch]
  - id: dispatch
    type: dynamic_next
    resolver: "/codelets/route_purpose"
edges:
  - from: start
    to: ask_purpose
  - from: ask_purpose
    to: dispatch
# --- END AgenticDSL ---
```

### AgenticDSL `/codelets/route_purpose`

```yaml
# --- BEGIN AgenticDSL ---
type: codelet
runtime: python3.10
code: |
  def main(context):
      purpose = context.get("llm_output", "").lower()
      if "summarize" in purpose:
          return {"next": ["summarize_doc"]}
      elif "translate" in purpose:
          return {"next": ["translate_text"]}
      else:
          return {"next": ["unknown_intent"]}
# --- END AgenticDSL ---
```
```

---

这份规范可直接作为 **system prompt** 或 **few-shot 示例的上下文** 提供给 LLM，确保其输出可被 AgenticDSL 引擎正确解析和执行。如需生成特定领域的模板（如客服、数据分析），可在此基础上扩展节点语义。
