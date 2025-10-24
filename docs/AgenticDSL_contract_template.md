# 📄 Standard Library v1.1 契约化模板

```yaml
### AgenticDSL `/lib/<domain>/<name>`
# --- BEGIN AgenticDSL ---
# 元信息：描述、作者、稳定性
metadata:
  description: "简明描述该子图的功能"
  author: "org.standard-library"
  tags: ["human", "safe", "soft-terminate"]  # 可选
  stability: "stable"  # stable | experimental | deprecated

# 【v1.1 强制】子图契约：定义输入/输出接口
signature:
  inputs:
    - name: lib_<domain>_<param1>
      type: string        # 支持: string, integer, boolean, array, object
      required: true      # true | false
      description: "参数用途说明"
    - name: lib_<domain>_<param2>
      type: array<string>
      required: false
      description: "可选参数说明"
  outputs:
    - name: lib_<name>_output  # 或具体字段如 lib_human_response
      type: object
      description: "输出结构说明"
      # 可选：JSON Schema 校验（执行器可选实现）
      schema:
        type: object
        properties:
          intent:
            type: string
          raw:
            type: string
        required: [intent, raw]

# 【v1.1 强制】权限声明：最小权限原则
permissions:
  - tool: request_human_intent           # 必须由执行器注册
  # - runtime: python3                   # 仅 codelet 需要
  #   allow_imports: [json, re]
  # - network: outbound
  #   domains: ["api.example.com"]

# 【可选】依赖声明（v1.1 支持）
requires:
  - lib: "/lib/utils/assign_from_template@^1.0"

# 节点定义（通常为 start → 工具/代码 → end）
type: tool_call
tool: request_human_intent
arguments:
  prompt: "{{ lib_<domain>_prompt }}"
  options: "{{ lib_<domain>_options | default([]) }}"
output_mapping:
  response: "lib_<name>_output"

# 终止模式：soft（推荐）或 hard
next: "/end_soft"
# --- END AgenticDSL ---

### AgenticDSL `/end_soft`
# --- BEGIN AgenticDSL ---
type: end
termination_mode: soft
# --- END AgenticDSL ---
```

---

## 🧩 字段填写指南

| 占位符 | 说明 | 示例 |
|--------|------|------|
| `<domain>` | 功能域 | `human`, `error`, `auth`, `data`, `flow` |
| `<name>` | 子图名称 | `clarify_intent`, `switch`, `validate_email` |
| `lib_<domain>_<param>` | 输入参数前缀 | `lib_human_prompt`, `lib_switch_on` |
| `lib_<name>_output` | 输出字段（推荐统一命名） | `lib_clarify_intent_output` 或 `lib_human_response` |
| `schema` | 可选 JSON Schema | 用于强类型校验（执行器可选支持） |

---

## ✅ 合规性检查清单（执行器加载时验证）

- [ ] 路径以 `/lib/` 开头  
- [ ] 包含 `signature` 字段  
- [ ] 所有 `inputs` 字段以 `lib_` 开头  
- [ ] 声明了必要的 `permissions`  
- [ ] `outputs` 字段在执行结束前被写入  
- [ ] 无跨命名空间跳转（如跳转到 `/main/...`）  
- [ ] 若为 `soft` 终止，必须以 `/end_soft` 结尾

---

## 📌 示例：`/lib/flow/switch`（v1.1）

```yaml
### AgenticDSL `/lib/flow/switch`
# --- BEGIN AgenticDSL ---
metadata:
  description: "基于上下文字段值动态路由"
  author: "org.standard-library"
  stability: "stable"

signature:
  inputs:
    - name: lib_switch_on
      type: string
      required: true
      description: "要判断的上下文字段路径（如 'user.intent'）"
    - name: lib_switch_cases
      type: object
      required: true
      description: "值到路径的映射，如 {'查订单': '/order/lookup'}"
    - name: lib_switch_default
      type: string
      required: false
      description: "默认跳转路径"
  outputs: []  # 无输出

permissions: []  # 无外部依赖

# 动态路由逻辑（由执行器内联处理，此处仅为占位）
type: assign
assign:
  _dummy: "switch logic handled by runtime"
next: "{{ lib_switch_cases[context[lib_switch_on]] | default(lib_switch_default) }}"
# --- END AgenticDSL ---
```

> 💡 注：部分控制流库（如 `switch`）可由执行器**内联优化**，无需实际节点执行。

