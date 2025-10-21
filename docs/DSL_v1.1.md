# 📘 AgenticDSL 规范 v1.1  
## **执行-生成协同版（最终修订版）**

> **定位**：为 Agentic 系统提供一套 **LLM 可生成、引擎可执行、动态可扩展** 的计算图语言标准。  
> **核心理念**：**生成即执行** —— LLM 输出的内容无需转换，可直接作为执行输入。  
> **四大原则**：
> 1. **统一结构**：执行文件 = LLM 生成片段的集合  
> 2. **路径即标识**：全局唯一路径替代标题锚点  
> 3. **Inja 为核**：所有动态逻辑通过 Inja 模板表达  
> 4. **资源为源**：外部系统通过资源节点注入上下文，**仅作只读输入**

---

## 一、公共部分（Public Contract）

所有 AgenticDSL 实现（执行器、LLM 提示、解析器）必须遵守以下约定。

### 1.1 上下文模型（Context）
- 全局可变字典，支持嵌套访问（如 `user.name`, `context.search_result`）
- 所有节点共享同一上下文；`assign` / `codelet` 返回值 **merge** 到该上下文
- 未定义变量渲染为空字符串（或由执行器策略决定是否报错）
- **上下文可包含任意类型值**（包括 tensor、DataFrame 等），但 **Inja 模板仅能安全处理可字符串化内容**

### 1.2 Inja 模板引擎（安全模式）
所有模板字段（`prompt_template`, `assign`, `arguments`）必须使用 **Inja 语法**，但执行器必须启用 **安全限制**：

✅ **允许**：
- 变量：`{{ var }}`, `{{ obj.field }}`
- 条件：`{% if ... %}...{% endif %}`
- 循环：`{% for item in list %}...{% endfor %}`（含 `loop.index`, `loop.is_last`）
- 内置函数：`length()`, `join()`, `default()`, `exists()`, `isString()`, `round()`, `sort()`, `upper()`, `lower()` 等
- 表达式：`{{ a > 0 and b }}`, `{{ x + 1 }}`, `{{ not flag }}`
- 模板内赋值：`{% set tmp = value %}`（仅影响模板渲染上下文）

❌ **禁止**（即使 Inja 支持）：
- `include` / `extends`（无外部模板文件）
- 访问系统环境变量（如 `env.HOME`）
- 自定义函数注册（除非执行器显式允许）
- 任意代码执行（如 `{{ eval(...) }}`）

> 💡 **最佳实践**：复杂对象（如 tensor）应在 `codelet` 中转换为语义化文本后再供 LLM 使用。

### 1.3 节点通用字段

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `type` | string | ✅ | 节点类型（见下表） |
| `next` | string 或 list | ⚠️ | 静态后继路径（除 `end` 外建议提供） |
| `metadata` | map | ❌ | 描述性信息（`description`, `author`, `tags` 等） |

### 1.4 节点类型与 v1 支持状态

| 类型 | v1_supported | 公共行为 | 字段对齐说明 |
|------|--------------|--------|-------------|
| `start` | ✅ | 无操作，跳转到 `next` | 无特殊字段 |
| `end` | ✅ | 终止执行，返回上下文 | 可选 `output_keys` |
| `assign` | ✅ | Inja 赋值到上下文 | **统一用 `assign`**（原 `set` 废弃） |
| `llm_call` | ✅ | 渲染提示 → 调用 LLM → 存结果 | `prompt_template` (Inja), `output_keys` |
| `tool_call` | ✅ | 渲染参数 → 调用工具 → 存结果 | `tool`, `arguments` (Inja), `output_keys` |
| `codelet` | ❌ | 定义可执行代码单元 | `runtime`, `code`（v2+） |
| `codelet_call` | ❌ | 调用 codelet | `codelet`（路径，v2+） |
| `dynamic_next` | ❌ | 动态路由节点 | `resolver`（codelet 路径，v2+） |
| `resource` | ✅ | **声明外部资源引用（只读）** | `resource_type`, `uri`, `scope` |

> ✅ **关键统一**：
> - **`assign`** 替代 `set`
> - **`arguments`** 替代 `args`
> - **`output_keys`** 替代 `output_key`（支持字符串或列表）

---

## 二、统一文档结构（Execution & Generation）

> **执行文件 = LLM 生成内容的直接拼接**  
> 不再区分“执行格式”与“生成格式”。

### 2.1 基本单元：路径化块（Pathed Block）

每个计算图元素必须以 **三级 Markdown 标题** 开始：

```markdown
### AgenticDSL `<路径>`
```

- `<路径>` 格式：`/命名空间/.../名称`
- 必须以 `/` 开头
- 仅允许字母、数字、`_`、`-`
- **若以 `/` 结尾（如 `/auth/`）→ 表示子图**；否则 → 单个节点

> 📌 **推荐路径约定**（非强制）：
> - `/main`：主流程
> - `/tools/xxx`：工具封装
> - `/codelets/xxx`：代码单元
> - `/resources/xxx`：资源声明
> - `/lib/xxx`：可复用子图

### 2.2 YAML 内容边界

路径标题下方必须包含 **YAML 内容**，且严格包裹在边界注释中：

```yaml
# --- BEGIN AgenticDSL ---
<YAML 内容>
# --- END AgenticDSL ---
```

> ✅ 此格式可被 LLM 精确生成，也可被执行器直接解析，**无需转换**。

### 2.3 入口约定

- **默认入口路径：`/main`**
- `/main` 必须是一个 `graph_type: subgraph`，含 `entry` 字段
- 支持多入口（如 `/debug`, `/test`），由调用方指定

### 2.4 文件级元信息（可选）

```markdown
### AgenticDSL `/__meta__`

# --- BEGIN AgenticDSL ---
version: "1.1"
requires: ["tool:web_search", "resource:weather_cache"]
description: "天气查询 Agent"
# --- END AgenticDSL ---
```

> 执行器可据此做依赖检查、版本兼容性判断。

---

## 三、节点、子图与资源规范

### 3.1 单节点定义（v1 支持）

```markdown
### AgenticDSL `/main/ask_user`

# --- BEGIN AgenticDSL ---
type: llm_call
prompt_template: "Hello! What would you like to do today?"
output_keys: "user_intent"
next: ["/main/dispatch"]
metadata:
  description: "获取用户初始意图"
# --- END AgenticDSL ---
```

### 3.2 资源节点（Resource Node）——只读输入源

```markdown
### AgenticDSL `/resources/user_db`

# --- BEGIN AgenticDSL ---
type: resource
resource_type: postgres
uri: "postgresql://user:pass@localhost/mydb"
scope: global
metadata:
  description: "用户主数据库连接"
# --- END AgenticDSL ---
```

#### 字段说明

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `type` | string | ✅ | 固定为 `"resource"` |
| `resource_type` | string | ✅ | `file`, `postgres`, `mysql`, `sqlite`, `api_endpoint`, `vector_store`, `custom` |
| `uri` | string | ✅ | 资源定位符 |
| `scope` | string | ❌ | `global`（默认）或 `local` |

#### 引用方式
- 自动注入上下文的 `resources` 命名空间
- Inja 中引用：`{{ resources.user_db.uri }}`
- **资源命名空间为只读**，不可通过 `{% set %}` 修改

#### 使用示例
```yaml
type: tool_call
tool: db_query
arguments:
  connection: "{{ resources.user_db.uri }}"
  sql: "SELECT name FROM users WHERE id = {{ user_id }}"
output_keys: "user_name"
```

> ⚠️ **禁止**：LLM 不得尝试写入 `resources.xxx`；写入必须通过专用工具。

---

## 四、执行语义（v1 能力集）

| 能力 | 支持情况 |
|------|--------|
| 节点类型 | `start`, `end`, `assign`, `llm_call`, `tool_call`, `resource` |
| 控制流 | 线性链（通过 `next`） |
| 模板 | 完整 Inja（安全模式） |
| 子图 / Codelet / dynamic_next | 可解析，v1 引擎忽略（不报错） |
| 并发/分支 | ❌ 不支持（v1 仅线性执行） |
| 资源节点 | ✅ 可解析并注入 `resources` 上下文（只读） |

> ✅ **向前兼容**：v1 引擎应忽略未知字段/类型，不报错。

---

## 五、LLM 生成指令（System Prompt 片段）

当要求 LLM 生成 AgenticDSL 时，请附加以下指令：

> 请严格按以下格式输出计算图：
> 1. 每个节点或子图以 `### AgenticDSL '/path'` 开头
> 2. 路径必须全局唯一，以 `/` 开头，仅含字母、数字、`_`、`-`
> 3. YAML 内容必须包裹在 `# --- BEGIN AgenticDSL ---` 和 `# --- END AgenticDSL ---` 之间
> 4. 使用 `assign`（非 `set`），`arguments`（非 `args`），`output_keys`（非 `output_key`）
> 5. 所有动态逻辑用 Inja 表达式（`{{}}` 包裹）
> 6. 子图必须声明 `graph_type: subgraph` 并定义 `entry`
> 7. 如需引用外部系统（文件、数据库等），先定义 `resource` 节点，再通过 `{{ resources.name... }}` 引用
> 8. **资源节点仅用于读取。不要尝试修改 `resources.xxx`。写入操作必须通过专用工具完成。**

---

## 六、完整示例：天气查询 Agent（v1.1 最终版）

```markdown
### AgenticDSL `/resources/weather_cache`
```yaml
# --- BEGIN AgenticDSL ---
type: resource
resource_type: file
uri: "/tmp/weather_cache.json"
scope: global
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
    next: [prepare]
  - id: prepare
    type: assign
    assign:
      location: "{{ user_input }}"
    next: [call_weather]
# --- END AgenticDSL ---
```

### AgenticDSL `/main/call_weather`

```yaml
# --- BEGIN AgenticDSL ---
type: tool_call
tool: http_get
arguments:
  url: "https://api.weather.com/v1/forecast?loc={{ location }}"
  cache_path: "{{ resources.weather_cache.uri }}"
output_keys: "weather_raw"
next: [generate_response]
# --- END AgenticDSL ---
```

### AgenticDSL `/main/generate_response`

```yaml
# --- BEGIN AgenticDSL ---
type: llm_call
prompt_template: |
  Current weather: {{ weather_raw }}
  Summarize concisely for user in {{ location }}.
output_keys: "final_answer"
next: [end]
# --- END AgenticDSL ---
```

### AgenticDSL `/main/end`

```yaml
# --- BEGIN AgenticDSL ---
type: end
# --- END AgenticDSL ---
```
```

---

## ✅ 总结

AgenticDSL v1.1 实现了：
- **结构统一**：LLM 生成 ⇔ 执行输入
- **语义清晰**：明确 v1/v2+ 支持边界
- **安全可靠**：Inja 安全模式 + 资源只读
- **工程友好**：支持元信息、路径约定
- **动态闭环**：执行 → LLM 生成 → 注入 → 继续执行

> **AgenticDSL v1.1 是迈向“自生成、自执行、自感知外部世界”的智能体的关键一步**。
