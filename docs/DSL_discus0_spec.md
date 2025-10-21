在 **AgenticFlow DSL v1 规范** 中，**节点（Node）** 是计算图的基本执行单元。每个节点代表一个原子操作，并通过 `next` 字段形成控制流。DSL v1 定义了 **5 种标准节点类型**，全部设计为 **无状态、纯函数式、LLM 可生成**，且 **不包含高层语义（如记忆、角色、目标）**。

以下是 DSL v1 中每种节点类型的详细解释：

---

### 🧱 1. `start` — 起始节点

- **作用**：标识图的入口点。
- **是否必需**：是（每个图必须有且仅有一个逻辑起点）。
- **字段**：
  ```yaml
  id: start
  type: start
  next: <下一个节点ID>
  ```
- **执行行为**：
  - 不修改上下文（context）
  - 仅跳转到 `next` 节点
- **示例**：
  ```yaml
  - id: start
    type: start
    next: get_user_query
  ```

> 💡 提示：实际执行引擎可自动插入 `start` 节点，用户 DSL 中可省略。

---

### 🧱 2. `end` — 终止节点

- **作用**：标识图的正常结束。
- **字段**：
  ```yaml
  id: end
  type: end
  ```
- **执行行为**：
  - 停止执行
  - 返回当前上下文作为最终结果
- **示例**：
  ```yaml
  - id: end
    type: end
  ```

> ⚠️ 注意：若图中存在未连接到 `end` 的路径，执行器应在编译时警告或报错。

---

### 🧱 3. `set` — 变量赋值节点

- **作用**：向上下文（context）中写入一个或多个变量，支持表达式渲染。
- **字段**：
  ```yaml
  id: <ID>
  type: set
  assign:
    <变量名>: "<模板字符串>"
  next: <下一个节点ID>
  ```
- **表达式语法**：使用 `{{...}}` 引用上下文中的变量（Jinja2 风格，仅变量访问，无逻辑/函数）。
- **执行行为**：
  - 对 `assign` 中每个值进行模板渲染
  - 将结果存入上下文
- **示例**：
  ```yaml
  - id: prepare_query
    type: set
    assign:
      search_query: "weather in {{user_location}}"
      log_msg: "Processing request from {{user_id}}"
    next: call_search
  ```

> 🔒 安全：`{{}}` 仅支持变量路径（如 `user.profile.name`），不支持任意代码。

---

### 🧱 4. `llm_call` — 本地 LLM 调用节点

- **作用**：调用底层 LLM（如 llama.cpp）生成文本。
- **字段**：
  ```yaml
  id: <ID>
  type: llm_call
  prompt_template: "<提示模板>"
  output_key: "<结果存入上下文的键名>"
  next: <下一个节点ID>
  ```
- **执行行为**：
  1. 渲染 `prompt_template`（支持 `{{}}`）
  2. 调用 LLM 生成响应
  3. 将响应字符串存入 `context[output_key]`
- **示例**：
  ```yaml
  - id: summarize
    type: llm_call
    prompt_template: "Summarize in one sentence: {{search_result}}"
    output_key: summary
    next: end
  ```

> ⚙️ 适配：LLM 后端通过 `AgenticFlow` 初始化时注入（如 `llama_generate` 函数）。

---

### 🧱 5. `tool_call` — 工具调用节点

- **作用**：调用注册的外部工具函数（如搜索、计算、API）。
- **字段**：
  ```yaml
  id: <ID>
  type: tool_call
  tool: "<工具名>"
  args:
    <参数名>: "<模板字符串>"
  output_key: "<结果存入上下文的键名>"
  next: <下一个节点ID>
  ```
- **执行行为**：
  1. 渲染 `args` 中所有值
  2. 调用 `tools[tool_name](**rendered_args)`
  3. 将返回值存入 `context[output_key]`
- **示例**：
  ```yaml
  - id: call_search
    type: tool_call
    tool: web_search
    args:
      query: "{{search_query}}"
    output_key: search_result
    next: summarize
  ```

> 🔌 工具注册：由上层 Agent 在运行前通过 `engine.register_tool(name, func)` 注入。

---

## 📌 共同约束（所有节点）

| 约束 | 说明 |
|------|------|
| **`id` 唯一性** | 图中所有节点 `id` 必须全局唯一 |
| **`next` 可选** | 除 `end` 外，其他节点应有 `next`；若缺失，视为连接到 `end` |
| **无副作用** | 节点只能通过 `context` 通信，不能直接访问外部状态（除非通过工具） |
| **纯执行** | 节点不包含“意图”“目标”“反思”等高层逻辑 |

---

## 🧩 未来扩展（v2+ 预留）

- `subgraph`：引用其他子图（静态复用）
- `condition`：条件分支（需安全表达式评估）
- `parallel`：并行执行多个子路径
- `retry`：失败重试策略

但 **DSL v1 保持极简**，仅包含上述 5 种节点，确保：
- LLM 能稳定生成
- 执行引擎易于实现
- 上层系统可自由组合


## **`codelet`（代码片段） + `codelet_call`（调用节点）**

这是一种 **“生成-验证-执行”三阶段模式**，而非“生成即执行”。


## 🧩 v2+ 新增节点类型建议

### 1. `codelet` 节点（声明式代码定义）

- **作用**：定义一段 LLM 生成的代码片段（如 Python 函数），但**不立即执行**。
- **关键**：代码必须符合**受限语法**（如仅允许表达式、无 import、无 I/O）。
- **字段**：
  ```yaml
  id: data_processor
  type: codelet
  language: python  # 未来可扩展 js, sql 等
  code: |
    def transform(data):
        return [x.upper() for x in data if len(x) > 3]
  output_key: fn_ref  # 存入上下文的是函数引用（或序列化标识）
  ```

> ⚠️ 实际存储的不是原始代码，而是**经验证的可执行对象**（如 `Callable` 或沙箱句柄）。

---

### 2. `codelet_call` 节点（安全调用）

- **作用**：调用已注册的 `codelet`，传入参数。
- **字段**：
  ```yaml
  id: run_transform
  type: codelet_call
  codelet_ref: "data_processor"  # 引用已定义的 codelet
  args:
    data: "{{raw_list}}"
  output_key: processed_data
  next: end
  ```

---

## 🔒 安全与控制机制（必须实现）

| 机制 | 说明 |
|------|------|
| **1. 语法限制器（Sandboxed Parser）** | 使用 `ast.parse` + 白名单 AST 节点（如只允许 `FunctionDef`, `ListComp`, `Call` 到内置函数） |
| **2. 执行沙箱（Restricted Eval）** | 在 `RestrictedPython` 或自定义 `exec` 环境中运行，禁用 `__import__`, `open`, `os` 等 |
| **3. 超时与资源限制** | 单次执行 ≤ 100ms，内存 ≤ 10MB |
| **4. 审计日志** | 所有生成的 codelet 必须记录原始代码、生成者、时间戳 |
| **5. 可选审批模式** | 在高安全场景，codelet 需人工审核后才可调用 |

> 🛑 **绝对禁止**：`eval("{{llm_output}}")` 或直接 `exec(llm_code)`。

---

## 🤖 LLM 如何生成 `codelet`？

通过**结构化提示词**强制 LLM 输出合规代码：

```text
你是一个数据处理函数生成器。请生成一个 Python 函数，满足：
- 函数名为 `transform`
- 输入参数为 `data`（list of str）
- 输出为处理后的 list
- 仅使用内置函数（如 len, str.upper），禁止 import 或 I/O

输出格式：
```python
def transform(data):
    # your code here
```
```

然后系统自动包装为 `codelet` 节点。

---

## 🧱 与项目定位对齐

| 你的项目原则 | 是否满足 |
|--------------|--------|
| **不负责高层语义** | ✅ `codelet` 只是“工具生成器”，不涉及记忆/角色 |
| **专注执行内核** | ✅ 提供安全的代码执行原语，类似 `tool_call` 的扩展 |
| **可被上层集成** | ✅ 上层 Agent 可选择是否启用 codelet 功能（默认关闭） |
| **LLM 友好 DSL** | ✅ LLM 可生成结构化 codelet，符合 YAML 嵌入规范 |

> 💡 **关键区别**：  
> - `tool_call` → 调用**开发者预置**的工具  
> - `codelet_call` → 调用**LLM 生成但经验证**的临时工具

---

## 📅 建议纳入 v2 路线图

| 版本 | 功能 |
|------|------|
| **v1** | 仅 `tool_call`（安全、静态工具） |
| **v2-alpha** | 实验性 `codelet` + 沙箱（默认关闭，需显式启用） |
| **v2-stable** | 完整 `codelet` 支持 + 审计日志 + 资源限制 |

---

## ❌ 不推荐的做法

- 直接添加 `exec_code` 节点执行任意字符串
- 允许 LLM 生成 `import os; os.system(...)` 类代码
- 在无沙箱环境下运行生成代码

---

## ✅ 总结

> **是的，你可以在 v2+ 中支持 LLM 生成代码的能力，但必须通过 `codelet` + `codelet_call` 的受控机制实现，而非开放任意代码执行。**

这既满足了 **“动态生成执行逻辑”** 的需求，又坚守了 **“安全、可审计、可嵌入”** 的项目边界。

如果你同意，我可以：
- 提供 `codelet` 节点的完整 YAML 示例
- 给出 Python 沙箱执行器的参考实现（基于 `RestrictedPython` 或 `ast` 白名单）

这个设计将使 AgenticFlow 成为 **首个支持“安全动态代码生成”的 LLM-native 执行引擎**。
---

## ✅ 总结表

| 类型 | 修改上下文？ | 调用外部？ | 控制流 | 典型用途 |
|------|--------------|------------|--------|--------|
| `start` | ❌ | ❌ | 跳转 | 入口 |
| `end`   | ❌ | ❌ | 终止 | 出口 |
| `set`   | ✅ | ❌ | 跳转 | 变量准备 |
| `llm_call` | ✅ | ✅（LLM） | 跳转 | 内容生成/推理 |
| `tool_call` | ✅ | ✅（工具） | 跳转 | 外部能力调用 |

---

这个设计确保 **AgenticFlow v1 是一个最小但完备的“LLM 可编程执行内核”**，为上层智能体提供可靠、可预测、可扩展的基础。
