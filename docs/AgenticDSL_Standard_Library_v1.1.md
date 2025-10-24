以下是 **AgenticDSL Standard Library v1.1 规范文档**，作为对 v1.0 的正式演进，全面对齐 **AgenticDSL v2.3 规范**，引入 **子图契约（Signature）、权限声明（Permissions）、版本语义、可观测埋点** 等核心机制，使标准库从“可参考模块”升级为“可验证、可组合、可审计”的智能体 SDK。

---

# 📚 AgenticDSL Standard Library v1.1  
**官方预置子图库 · 契约化 · 安全沙箱 · 版本可控 · 可观测**

> **定位**：为 LLM 程序员提供**带接口契约、权限隔离、版本管理**的高频复用子图，覆盖人类交互、错误恢复、数据处理、身份认证、流程控制等场景。  
> **原则**：**优先调用标准库，禁止重复生成逻辑**；所有 `/lib/**` 子图必须声明 `signature` 与 `permissions`。  
> **调用方式**：`next: "/lib/<domain>/<name>@v1"`（推荐带版本）  
> **命名空间**：所有库子图位于 `/lib/...` 路径下，禁止跨内部节点跳转。

---

## 一、通用规范（v1.1 强制要求）

### 1.1 子图契约（Signature）

每个库子图必须在元信息中声明结构化接口：

```yaml
signature:
  inputs:
    - name: lib_<domain>_<field>
      type: string | integer | boolean | array | object
      required: true | false
      description: "..."
  outputs:
    - name: lib_<name>_output  # 或特定字段（如 lib_human_response）
      type: object
      schema: { ... }  # 可选 JSON Schema
  version: "1.1"
  stability: "stable"  # stable / experimental / deprecated
```

> ✅ 执行器必须在调用前校验 `inputs` 存在性，调用后验证 `outputs` 写入。

### 1.2 权限声明（Permissions）

每个库子图必须声明所需最小权限：

```yaml
permissions:
  - tool: request_human_intent          # 必须注册
  - runtime: python3                    # 仅用于 codelet
    allow_imports: [re, json]
  - network: outbound
    domains: ["auth.example.com"]
```

> ✅ 执行器对 `/lib/**` 启用沙箱，拦截未授权行为。

### 1.3 版本与路径

- 所有库路径支持语义化版本后缀：`@v1`, `@v1.1`, `@latest`
- 推荐调用方式：`next: "/lib/human/clarify_intent@v1"`
- `@latest` 仅用于开发，**生产环境禁用**

### 1.4 可观测埋点

- 所有 `/lib/**` 节点自动附加标签：
  ```json
  {
    "node_type": "standard_library",
    "lib_path": "/lib/human/clarify_intent",
    "lib_version": "1.1"
  }
  ```
- Trace 日志包含输入快照（脱敏）、输出结构、执行状态

---

## 二、目录结构（v1.1 新增模块）

```
/lib
  /human
    /clarify_intent        # v1.1: 契约化 + 权限
    /confirm_action        # v1.1: 契约化 + 权限
  /error
    /retry_with_backoff    # v1.1: 契约化 + 幂等校验
    /fallback_to_default   # v1.1: 契约化
  /auth
    /verify_session        # v1.1: 契约化 + 权限
    /login_required        # 系统预置
  /data
    /validate_email        # v1.1: 契约化
    /extract_entities      # v1.1: 契约化
  /flow                  # ← v1.1 新增
    /switch              # 基于字段值动态路由
    /parallel_map        # 并行映射处理列表
  /utils
    /noop                # v1.1: 契约化（空输入/输出）
    /assign_from_template# v1.1: 契约化
```

---

## 三、模块详情（v1.1 契约化版）

### 3.1 `/lib/human/clarify_intent`

- **描述**：请求人类澄清模糊意图
- **Signature**:
  ```yaml
  inputs:
    - name: lib_human_prompt
      type: string
      required: true
    - name: lib_human_options
      type: array<string>
      required: false
  outputs:
    - name: lib_human_response
      type: object
      schema: { intent: string, raw: string }
  ```
- **Permissions**:
  ```yaml
  - tool: request_human_intent
  ```
- **Termination**: `soft`
- **Trace Label**: `lib_human_clarify`

---

### 3.2 `/lib/flow/switch` ← **v1.1 新增**

- **描述**：基于上下文字段值动态路由（替代复杂 Inja）
- **Signature**:
  ```yaml
  inputs:
    - name: lib_switch_on
      type: string
      required: true          # 字段路径（如 "user.intent"）
    - name: lib_switch_cases
      type: object
      required: true          # { "value1": "/path1", "value2": "/path2" }
    - name: lib_switch_default
      type: string
      required: false         # 默认路径
  outputs: []  # 无输出
  ```
- **示例**：
  ```yaml
  assign:
    lib_switch_on: "user.intent"
    lib_switch_cases:
      "查订单": "/order/lookup"
      "改地址": "/profile/update"
    lib_switch_default: "/lib/human/clarify_intent"
  next: "/lib/flow/switch@v1"
  ```
- **Permissions**: none
- **Termination**: none（仅路由）

---

### 3.3 `/lib/flow/parallel_map` ← **v1.1 新增**

- **描述**：对列表元素并行执行子任务
- **Signature**:
  ```yaml
  inputs:
    - name: lib_map_input_list
      type: array
      required: true
    - name: lib_map_item_key
      type: string
      required: true        # 临时字段名（如 "current_item"）
    - name: lib_map_target
      type: string
      required: true        # 子图路径（如 "/process/item"）
  outputs:
    - name: lib_map_results
      type: array<object>   # 每个元素为子图输出快照
  ```
- **行为**：
  - 为每个元素创建独立上下文副本
  - 并行执行 `lib_map_target`
  - 按顺序合并结果到 `lib_map_results`
- **Permissions**: none（依赖目标子图自身权限）
- **合并策略**：强制 `error_on_conflict`（避免副作用）

---

### 3.4 其他模块（简略，均契约化）

| 路径 | 输入契约摘要 | 输出契约摘要 | 权限 |
|------|------------|------------|------|
| `/lib/error/retry_with_backoff` | `lib_retry_target: string`, `lib_retry_input: object` | `lib_retry_result: any` | none |
| `/lib/auth/verify_session` | 从 `session.token` 自动读取 | `lib_auth_output: {user_id, is_valid}` | `tool: session_store` |
| `/lib/data/validate_email` | `lib_email_input: string` | `lib_email_output: {is_valid, normalized}` | `codelet: validate_email` |
| `/lib/utils/assign_from_template` | `lib_template`, `lib_context`, `lib_output_key` | `<lib_output_key>: string` | none |

---

## 四、使用说明（v1.1）

### 4.1 如何调用？

```yaml
type: assign
assign:
  lib_human_prompt: "请确认操作"
  lib_human_options: ["是", "否"]
next: "/lib/human/confirm_action@v1"  # ← 带版本
```

### 4.2 如何传参？

- 通过 `assign` 写入 `lib_*` 字段
- 字段名必须与 `signature.inputs` 严格一致

### 4.3 如何获取结果？

- 输出字段由 `signature.outputs` 明确声明
- 父图可直接使用（如 `{{ lib_human_response.intent }}`）

### 4.4 如何覆盖？

- LLM 可生成同路径子图并声明：
  ```yaml
  metadata:
    override: true
  ```
- **但必须保留相同 `signature`**，否则执行器拒绝加载

---

## 五、执行器责任（v1.1）

1. **启动时预加载** 所有 `/lib/**` 子图，并校验 `signature` 与 `permissions`
2. **注入可用库清单** 到 LLM prompt：
   ```jinja2
   available_subgraphs:
     - path: "/lib/flow/switch@v1"
       description: "基于字段值动态路由"
       inputs: ["lib_switch_on", "lib_switch_cases"]
       outputs: []
   ```
3. **调用时校验**：
   - 输入字段存在性
   - 权限是否满足
   - 输出是否按契约写入
4. **自动埋点**：为所有库调用生成结构化 trace

---

## 六、演进路线

- **v1.0 → v1.1**：契约化、权限化、新增流程控制模块
- **v1.2（规划中）**：支持异步回调、事件驱动、跨智能体协作

---

> **Standard Library v1.1 是 AgenticDSL 生态的“标准 SDK”。通过契约与权限，我们让 LLM 安全调用“函数”，而非“猜测黑盒”。**

--- 

如需各模块的完整 YAML 源码模板或测试用例，可继续提出。
