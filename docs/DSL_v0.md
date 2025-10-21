# 📘 AgenticDSL v1 规范 (Inja 增强版)

**LLM-Native 动态计算图语言标准（v1.0）**

> **定位**：为上层 Agentic 系统提供标准化、安全、可并发执行的计算图 DSL。  
> **不包含**：记忆、角色、目标规划、知识图谱等高层语义。  
> **核心能力**：静态图定义 + 动态扩展钩子（v2+） + 并发依赖调度 + **Inja 模板增强**。

---

## 1. 文档结构约定

DSL 以 **Markdown 文件** 为载体，使用以下结构：

```markdown
# <主图标题>

```yaml
nodes:
  - id: ...
    type: ...
```

## <子图标题> {#anchor-id}

```yaml
nodes:
  - id: ...
```
```

- 每个 `##` 标题块可选带 `{#anchor}` 作为唯一 ID（用于子图引用）
- 所有逻辑必须写在 **YAML fenced code block**（```yaml）中
- 仅允许一个顶层图（`#` 标题），其余为子图（`##`）

---

## 2. 图结构根元素

每个 YAML 块必须包含 `nodes` 列表：

```yaml
nodes:
  - id: start
    type: start
    next: step1
  - id: step1
    type: set
    ...
```

> ⚠️ 不支持全局字段（如 `version`, `metadata`）——这些由执行器通过 Markdown 元信息处理。

---

## 3. 节点类型规范（v1）

所有节点必须包含 `id` 和 `type`。除 `end` 外，应包含 `next`。

### 3.1 `start`：起始节点

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `id` | string | ✅ | 建议为 `"start"` |
| `type` | string | ✅ | 固定为 `"start"` |
| `next` | string | ✅ | 下一节点 ID |

**行为**：无操作，仅跳转。

**示例**：
```yaml
- id: start
  type: start
  next: get_input
```

---

### 3.2 `end`：终止节点

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `id` | string | ✅ | 建议为 `"end"` |
| `type` | string | ✅ | 固定为 `"end"` |

**行为**：停止执行，返回当前上下文。

**示例**：
```yaml
- id: end
  type: end
```

---

### 3.3 `set`：变量赋值

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `id` | string | ✅ | 节点唯一 ID |
| `type` | string | ✅ | 固定为 `"set"` |
| `assign` | map<string, string> | ✅ | 键为变量名，值为 **Inja 模板字符串** |
| `next` | string | ✅ | 下一节点 ID |

**模板语法**：**Inja 模板语法**，支持变量访问、条件、循环、函数等。

**示例**：
```yaml
- id: prepare
  type: set
  assign:
    # 简单变量替换
    query: "weather in {{user.location}}"
    # 条件逻辑
    greeting: "{% if user.profile.name %}Hello, {{ user.profile.name }}!{% else %}Hello!{% endif %}"
    # 循环和内置函数
    guest_list: "Guests: {{ join(user.guests, ', ') }}"
    guest_count: "{{ length(user.guests) }}"
    # 复杂表达式
    time_status: "{% if context.time.hour < 12 %}Morning{% elif context.time.hour < 18 %}Afternoon{% else %}Evening{% endif %}"
  next: search
```

---

### 3.4 `llm_call`：LLM 调用

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `id` | string | ✅ | 节点唯一 ID |
| `type` | string | ✅ | 固定为 `"llm_call"` |
| `prompt_template` | string | ✅ | **Inja 模板**提示字符串（支持所有 Inja 语法） |
| `output_key` | string | ✅ | 结果存入上下文的键名 |
| `next` | string | ✅ | 下一节点 ID |

**行为**：渲染 Inja 模板提示 → 调用 LLM → 存结果。

**示例**：
```yaml
- id: generate_response
  type: llm_call
  prompt_template: |
    {% if greeting %}{{ greeting }} {% endif %}
    {% if guest_count > 0 %}
    You have {{ guest_count }} guest{% if guest_count > 1 %}s{% endif %}:
    {% for guest in user.guests %}
    - {{ guest.name }} ({{ guest.role }})
    {% endfor %}
    {% endif %}
    
    {% if location %}
    The location is {{ location }}.
    {% endif %}
    
    {% if context.previous_results %}
    Previous results:
    {% for result in context.previous_results %}
    - {{ result.summary }}
    {% endfor %}
    {% endif %}
    
    Based on this information, provide a relevant response.
  output_key: llm_response
  next: end
```

---

### 3.5 `tool_call`：工具调用

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `id` | string | ✅ | 节点唯一 ID |
| `type` | string | ✅ | 固定为 `"tool_call"` |
| `tool` | string | ✅ | 工具名（必须已注册） |
| `args` | map<string, string> | ✅ | 参数 **Inja 模板**（支持所有 Inja 语法） |
| `output_key` | string | ✅ | 结果存入上下文的键名 |
| `next` | string | ✅ | 下一节点 ID |

**行为**：渲染 Inja 模板参数 → 调用工具函数 → 存结果。

**示例**：
```yaml
- id: search
  type: tool_call
  tool: web_search
  args:
    # 使用 Inja 模板构建查询
    query: "{{user.location}} {{time_status}} weather forecast"
  output_key: search_result
  next: summarize
```

---

## 4. 控制流与依赖

- 节点通过 `next` 形成**线性链**（v1 不支持分支/并行）
- 执行顺序 = 拓扑顺序（由 `next` 隐式定义）
- 循环引用将导致执行器报错

---

## 5. Inja 模板规则

**AgenticDSL 的 `prompt_template` 和 `assign` 字段值现在是完整的 Inja 模板。**

### 5.1 基础语法

- **变量**：`{{ variable_name }}` 或 `{{ object.field }}`
- **条件**：`{% if condition %}...{% elif condition2 %}...{% else %}...{% endif %}`
- **循环**：`{% for item in list %}...{% endfor %}` 或 `{% for key, value in object %}...{% endfor %}`
- **注释**：`{# comment #}`
- **行语句**：`## statement` (需要在环境配置中启用)

### 5.2 内置函数

支持 Inja 的所有内置函数，例如：

- `upper(str)`, `lower(str)`, `capitalize(str)`
- `length(list_or_string)`
- `join(list, separator)`
- `range(start, end)`
- `sort(list)`
- `first(list)`, `last(list)`
- `round(number, precision)`
- `default(value, default_value)`
- `exists(key)`, `existsIn(object, key)`
- `at(object, key)` / `at(list, index)`
- `int(str)`, `float(str)`
- `isString(value)`, `isArray(value)`, `isObject(value)` 等类型检查
- `odd(num)`, `even(num)`, `divisibleBy(num, divisor)`
- `max(list)`, `min(list)`

### 5.3 循环变量

在 `{% for %}` 循环内部，可以使用特殊变量：

- `loop.index` (0-based)
- `loop.index1` (1-based)
- `loop.is_first`
- `loop.is_last`
- `loop.parent` (在嵌套循环中访问父循环变量)

### 5.4 表达式

Inja 支持简单的表达式，如 `{{ time.start + 1 }}`, `{{ length(guests) > 0 }}`, `{{ a and b }}`, `{{ not condition }}`。

### 5.5 模板赋值

可以在模板内部使用 `{% set variable = value %}` 进行赋值，但这只影响渲染上下文，不会修改原始的 `Context`。

### 5.6 模板继承与包含

虽然 Inja 支持 `extends` 和 `include`，但在 AgenticDSL 的节点模板中使用这些功能可能不太实际，因为模板是内联定义的。此功能主要为 `InjaTemplateRenderer` 的更高级用例保留。

---

## 6. 完整示例：增强版天气查询 Agent

```markdown
# 🌤️ Enhanced Weather Assistant

```yaml
nodes:
  - id: start
    type: start
    next: prepare_context

  - id: prepare_context
    type: set
    assign:
      location: "{{ user_input }}"
      # 根据输入动态构建查询
      query_parts: |
        {% set parts = [] %}
        {% if user_input.location %}{{ parts.append(user_input.location) }}{% endif %}
        {% if user_input.type == "forecast" %}{{ parts.append("forecast") }}{% endif %}
        {% if user_input.days %}{{ parts.append(user_input.days ~ " days") }}{% endif %}
        {{ parts }}
      query_string: "{{ join(query_parts, ' ') }}"
    next: call_weather_api

  - id: call_weather_api
    type: tool_call
    tool: get_weather
    args:
      location: "{{ location }}"
      # 使用 Inja 模板构建更复杂的参数
      params: |
        {% set p = {} %}
        {% if query_string contains "forecast" %}{{ p.update({"forecast": true}) }}{% endif %}
        {{ p }}
    output_key: weather_data
    next: generate_response

  - id: generate_response
    type: llm_call
    prompt_template: |
      {% if weather_data.error %}
      I'm sorry, I couldn't retrieve the weather information for {{ location }}.
      {% else %}
      {% set temp = weather_data.temperature %}
      {% set desc = weather_data.description %}
      {% set details = weather_data.details %}
      
      {% if query_string contains "forecast" %}
      Here is the {{ query_string }} for {{ location }}:
      {% for day_data in details %}
      - **{{ day_data.date }}**: {{ day_data.condition }}, High: {{ day_data.high }}°, Low: {{ day_data.low }}°
      {% endfor %}
      {% else %}
      Current weather in {{ location }}: {{ desc }}, {{ temp }}°C.
      {% if details %}
      Additional details: {% for key, value in details.items() %}{{ key }}: {{ value }}; {% endfor %}
      {% endif %}
      {% endif %}
      {% endif %}
      
      Provide a concise and helpful response based on the above data.
    output_key: final_answer
    next: end

  - id: end
    type: end
```
```

> 💡 此文件利用 Inja 的条件、循环和函数功能，根据 `user_input` 的结构动态生成查询和响应。

---

## 7. 约束与最佳实践

| 类别 | 规则 |
|------|------|
| **命名** | 节点 `id` 使用 `snake_case`，全局唯一 |
| **工具** | `tool` 名称必须在执行前注册，否则运行时报错 |
| **LLM 提示** | 充分利用 Inja 的条件和循环来构建动态、上下文相关的提示。 |
| **错误处理** | v1 不支持 `on_error`，失败即终止（v2+ 引入重试/回退）。 |
| **扩展性** | 保留 `expansion` 字段（v2 动态图钩子），v1 可忽略。 |
| **性能**：虽然 Inja 功能强大，但复杂的模板逻辑可能影响渲染性能。对于简单变量替换，直接使用 `{{var}}` 即可。 |

---

## 8. 未来扩展预留（v2+）

以下字段/节点类型**在 v1 中无效**，但解析器应忽略（不报错）：

- 节点字段：`expansion`, `priority`, `timeout`
- 节点类型：`subgraph`, `codelet`, `codelet_call`, `condition`
- 元信息：`meta` 块（用于动态子图依赖声明）

> 这保证 v1 DSL 与未来版本**向前兼容**。

---

## ✅ 总结

AgenticDSL v1 (Inja 增强版) 是一个**极简、强大、LLM 友好**的计算图语言，包含：

- **5 种节点类型**：`start`, `end`, `set`, `llm_call`, `tool_call`
- **1 种文档格式**：Markdown + YAML fenced block
- **1 种模板语言**：**Inja** (支持变量、条件、循环、函数、表达式等)
- **0 高层语义**：专注执行，不越界

此规范可作为 **LLM 生成目标格式**、**人类编写模板** 和 **执行器解析契约** 的统一标准，同时利用 Inja 提供了强大的动态内容生成能力。
