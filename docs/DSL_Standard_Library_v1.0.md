# 📚 AgenticDSL Standard Library v1.0  
**官方预置子图库 · 可复用 · 可组合 · 安全**

> 本库为 LLM 程序员提供高频、通用、安全的子图模块，覆盖 **人类交互、错误恢复、数据处理、身份认证、工具封装** 等场景。  
> **使用原则**：优先调用标准库，避免重复生成逻辑。  
> **调用方式**：`next: "/lib/<domain>/<name>"`  
> **命名空间**：所有库子图位于 `/lib/...` 路径下，禁止跨内部节点跳转。

---

## 目录结构

```
/lib
  /human
    /clarify_intent        # 请求人类澄清模糊意图
    /confirm_action        # 请求人类确认高风险操作
  /error
    /retry_with_backoff    # 指数退避重试（最多3次）
    /fallback_to_default   # 降级到默认响应
  /auth
    /verify_session        # 验证用户会话有效性
    /login_required        # 跳转到登录流程（系统预置）
  /data
    /validate_email        # 校验并标准化邮箱格式
    /extract_entities      # 从文本提取结构化实体（调用 NER 工具）
  /utils
    /noop                  # 空操作（用于占位或调试）
    /assign_from_template  # 安全地从模板生成结构化输出
```

---

## 1. `/lib/human/clarify_intent`

**描述**：暂停执行，请求人类澄清用户模糊意图（适用于 LLM 无法确定场景）  
**输入**（通过 `assign` 设置）：
```yaml
assign:
  lib_human_prompt: string      # 显示给人类的问题（必填）
  lib_human_options: array?     # 可选按钮列表（如 ["查订单", "改地址"]）
```
**输出**：
```yaml
lib_human_response:
  intent: string      # 若提供选项，则为选中值；否则为原始输入
  raw: string         # 原始人类输入
```
**终止模式**：`soft`（仅结束本子图，返回父流程）  
**依赖工具**：`request_human_intent`（必须由执行器注册）  
**示例调用**：
```yaml
type: assign
assign:
  lib_human_prompt: "用户说‘还没到’，请判断真实意图"
  lib_human_options: ["查物流", "催发货", "投诉"]
next: "/lib/human/clarify_intent"
```

---

## 2. `/lib/human/confirm_action`

**描述**：请求人类确认高风险操作（如删除、支付）  
**输入**：
```yaml
assign:
  lib_confirm_prompt: string    # 操作描述（必填）
  lib_risk_level: string        # "low" | "medium" | "high"（影响 UI 提示）
```
**输出**：
```yaml
lib_confirm_result:
  confirmed: boolean
  reason: string?               # 若拒绝，可附理由
```
**行为**：若未确认，跳转到 `/end`（`soft` 终止）  
**依赖工具**：`request_human_confirmation`

---

## 3. `/lib/error/retry_with_backoff`

**描述**：对失败操作进行指数退避重试（1s, 2s, 4s）  
**输入**：
```yaml
assign:
  lib_retry_target: string    # 要重试的节点路径（如 "/main/api_call"）
  lib_retry_input: object     # 重试所需上下文快照（建议包含原始参数）
```
**行为**：
- 最多重试 3 次
- 成功后写入 `lib_retry_result`
- 失败后跳转到 `on_error` 或终止
**输出**：
```yaml
lib_retry_result: any         # 成功时的返回值
```
**注意**：目标节点必须是幂等的！

---

## 4. `/lib/error/fallback_to_default`

**描述**：当主流程失败时，返回友好默认响应  
**输入**：
```yaml
assign:
  lib_fallback_message: string  # 默认回复内容
```
**输出**：
```yaml
final_response: "{{ lib_fallback_message }}"
```
**终止模式**：`hard`（结束整个工作流）  
**用途**：作为 `on_error` 的兜底处理

---

## 5. `/lib/auth/verify_session`

**描述**：验证当前用户会话是否有效  
**输入**：自动从 `session.token` 读取  
**依赖工具**：`session_store`（需支持 `verify(token) → { user_id, valid }`）  
**输出**：
```yaml
lib_auth_output:
  user_id: string
  is_valid: boolean
```
**失败行为**：自动跳转到 `/lib/auth/login_required`  
**终止模式**：`soft`（允许父图处理未登录逻辑）

---

## 6. `/lib/data/validate_email`

**描述**：校验并标准化邮箱格式  
**实现**：调用内置 `codelet`  
**输入**：
```yaml
assign:
  lib_email_input: string
```
**输出**：
```yaml
lib_email_output:
  is_valid: boolean
  normalized: string?   # 小写标准化格式（如 "user@example.com"）
```
**依赖**：`/codelets/internal/validate_email`（执行器预置）

---

## 7. `/lib/utils/noop`

**描述**：空操作节点，用于占位、调试或默认分支  
**输出**：无  
**终止模式**：`soft`  
**示例**：
```yaml
next: "{% if debug %}/lib/utils/noop{% else %}/main/real_step{% endif %}"
```

---

## 8. `/lib/utils/assign_from_template`

**描述**：安全地从 Inja 模板生成结构化对象（避免 LLM 直接生成 JSON）  
**输入**：
```yaml
assign:
  lib_template: string          # 模板字符串（如 "订单{{ id }}状态{{ status }}"）
  lib_context: object           # 模板渲染上下文
  lib_output_key: string        # 输出字段名（如 "summary"）
```
**输出**：
```yaml
<lib_output_key>: string      # 渲染结果
```
**用途**：替代 LLM 生成自然语言摘要，提升可靠性

---

## 使用说明

### 如何调用？
LLM 程序员只需在 `next` 中跳转至库子图入口：
```yaml
next: "/lib/human/clarify_intent"
```

### 如何传参？
通过 `assign` 节点提前写入约定的 `lib_*` 字段：
```yaml
type: assign
assign:
  lib_human_prompt: "请确认操作"
  lib_human_options: ["是", "否"]
next: "/lib/human/confirm_action"
```

### 如何获取结果？
库子图输出统一写入 `lib_<name>_output` 或特定路径（见各模块说明），父图可直接使用。

### 如何覆盖？
如需定制库行为，LLM 可生成同路径子图并声明：
```yaml
metadata:
  override: true
```

### 执行器责任
- 启动时预加载所有 `/lib/**` 子图
- 在 `llm_call` 的 `execution_context` 中注入可用库清单：
  ```yaml
  available_subgraphs:
    - path: "/lib/human/clarify_intent"
      description: "请求人类澄清意图"
      input_keys: ["lib_human_prompt", "lib_human_options"]
      output_keys: ["lib_human_response"]
  ```

---

> **标准库是 AgenticDSL 生态的基石。通过复用，我们让 LLM 专注“编排”，而非“发明轮子”。**
