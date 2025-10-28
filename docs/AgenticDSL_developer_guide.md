# AgenticDSL v3.1 开发者指南  
**构建可验证、可进化的智能体工作流**

> **最后更新**：2025年10月28日  
> **适用对象**：AI 工程师、智能体开发者、LLM 应用架构师  
> **前置要求**：熟悉 AgenticDSL v3.1 规范（[链接](#)）

---

## 一、快速入门：5 分钟创建你的第一个智能体

### 1.1 安装工具链（推荐）
```bash
# 安装官方 CLI（含验证器、模拟器、可视化器）
pip install agentic-cli

# 验证安装
agentic --version  # 应输出 v3.1+
```

### 1.2 创建 `.agent.md` 文件
新建 `solve_math.agent.md`：

```markdown
### AgenticDSL `/__meta__`
yaml
version: "3.1"
mode: dev
execution_budget:
  max_nodes: 10
  max_subgraph_depth: 1

### AgenticDSL `/main/start`
yaml
type: assign
assign:
  expr: "x^2 - 4 = 0"
next: "/lib/reasoning/solve_quadratic@v1"

### AgenticDSL `/main/verify`
yaml
type: assert
condition: "len($.roots) == 2 and -2 in $.roots and 2 in $.roots"
on_success: "/main/end"
on_failure: "/self/repair"

### AgenticDSL `/self/repair`
yaml
type: generate_subgraph
prompt_template: "方程 {{ $.expr }} 求解失败。请生成修复DAG。"
signature_validation: warn
next: "/dynamic/repair_1"

### AgenticDSL `/main/end`
yaml
type: end
termination_mode: hard
```

### 1.3 本地模拟执行
```bash
# Dry-run 模式：不调用真实 LLM，使用 mock 响应
agentic simulate solve_math.agent.md --mock-llm

# 查看 Trace
agentic trace last
```

你将看到结构化执行日志，包括上下文变化、断言结果、预算使用等。

---

## 二、核心开发模式

### 2.1 开发模式 vs 生产模式

| 特性 | 开发模式 (`mode: dev`) | 生产模式 (`mode: prod`) |
|------|------------------------|--------------------------|
| 签名验证 | `warn`（允许不合规子图） | `strict`（强制合规） |
| 合并策略 | 允许 `last_write_wins` | 禁用 `last_write_wins` |
| Trace 详情 | 包含完整上下文快照 | 仅记录 delta |
| 权限沙箱 | 可关闭 | 强制启用 |

**最佳实践**：
- 开发阶段：使用 `dev` 模式快速迭代
- 上线前：切换为 `prod` 模式并通过 `agentic validate` 检查

### 2.2 三层抽象开发指南

#### ✅ 执行原语层（你几乎不需要写）
- 使用内置节点：`assign`, `assert`, `tool_call` 等
- **禁止**：在此层实现业务逻辑

#### ✅ 推理原语层（规范提供，可扩展）
- 复用 Core SDK：`/lib/reasoning/assert`, `/lib/human/approval`
- 扩展新推理模式：
  ```markdown
  ### AgenticDSL `/lib/reasoning/self_consistency@v1`
  yaml
  signature:
    inputs: [{ name: question, type: string }]
    outputs: [{ name: answer, type: string }]
    version: "1.0"
    stability: experimental
  # 实现：多次生成 + 投票
  ```

#### ✅ 知识应用层（你的主战场）
- 组合推理原语构建领域工作流：
  ```yaml
  next:
    - "/lib/reasoning/hypothesize@v1"
    - "/lib/reasoning/verify_solution@v1"
    - "/lib/human/approval@v1"
  ```

---

## 三、标准库开发：创建你的第一个 `/lib/**` 子图

### 3.1 必须包含的元素
每个标准库子图必须声明：

```yaml
signature:        # ✅ 接口契约
  inputs: [...]
  outputs: [...]
  version: "x.y"
  stability: stable|experimental|deprecated

permissions:      # ✅ 权限声明（如需要）
  - tool: web_search

# 节点逻辑（可包含多个节点）
type: ...
```

### 3.2 示例：创建数学验证器
```markdown
### AgenticDSL `/lib/reasoning/verify_quadratic_root@v1`
yaml
signature:
  inputs:
    - name: equation
      type: string
      required: true
    - name: root
      type: number
      required: true
  outputs:
    - name: is_valid
      type: boolean
      required: true
  version: "1.0"
  stability: stable

type: codelet_call
runtime: python3
code: |
  import re
  # 解析 ax^2 + bx + c = 0
  expr = "{{ $.equation }}"
  root = {{ $.root }}
  # ... 验证逻辑
  result = {"is_valid": True}
security: restricted
permissions:
  - runtime: python3 → allow_imports: [re, json]
```

### 3.3 发布与版本管理
- 语义化版本：`@v1`, `@v1.1`, `@v2`
- 依赖声明：
  ```yaml
  requires:
    - lib: "/lib/reasoning/assert@^1.0"
  ```
- 执行器启动时自动解析依赖图

---

## 四、调试与可观测性

### 4.1 结构化 Trace 字段说明
| 字段 | 用途 | 开发建议 |
|------|------|--------|
| `context_delta` | 本次节点修改的上下文 | 检查赋值是否符合预期 |
| `expected_output` vs `actual_output` | 验证结果 | 用于训练数据提取 |
| `budget_snapshot` | 资源使用 | 避免超限 |
| `llm_intent` | LLM 任务意图 | 分析生成质量 |
| `error_code` | 标准化错误码 | 自动化处理 |

### 4.2 常用调试命令
```bash
# 查看最近一次执行的详细 Trace
agentic trace last --format json

# 可视化 DAG 执行路径
agentic visualize solve_math.agent.md

# 提取训练三元组 (input, expected, actual)
agentic extract-train-data solve_math.agent.md
```

### 4.3 常见错误处理
| 错误码 | 含义 | 修复建议 |
|--------|------|--------|
| `ERR_CTX_MERGE_CONFLICT` | 上下文合并冲突 | 检查 `context_merge_policy` |
| `ERR_ASSERT_FAILED` | 断言失败 | 检查 `condition` 表达式 |
| `ERR_SIGNATURE_VIOLATION` | 签名不合规 | 检查 LLM 生成的子图 |
| `ERR_BUDGET_EXCEEDED` | 预算超限 | 增加 `max_nodes` 或优化 DAG |

---

## 五、高级模式：实现复杂工作流

### 5.1 并行搜索 + 结果聚合
```yaml
type: fork
branches:
  - "/dynamic/search_google"
  - "/dynamic/search_bing"
  - "/dynamic/search_duckduckgo"
next: "/main/merge_results"

### AgenticDSL `/main/merge_results`
yaml
type: assign
assign:
  path: "all_results"
  expr: "{{ $.google_results + $.bing_results + $.duck_results }}"
context_merge_policy:
  "all_results": "array_concat"  # ✅ 关键：使用数组拼接
```

### 5.2 人机协作审批流
```yaml
next: "/lib/human/approval@v1"

### AgenticDSL `/lib/human/approval@v1`
# （已在 Core SDK 中提供）
# 输出: { approved: bool, comment: string }

### AgenticDSL `/main/post_approval`
yaml
type: assign
assign:
  expr: "{{ $.approved ? 'proceed' : 'abort' }}"
next: "{{ $.proceed_next }}"
```

### 5.3 自修复循环
```yaml
on_failure: "/self/repair"

### AgenticDSL `/self/repair`
yaml
type: generate_subgraph
prompt_template: >
  当前任务失败。上下文：{{ $.ctx }}。
  请生成修复DAG，最多再调用1层子图。
signature_validation: warn
permissions:
  generate_subgraph: { max_depth: 1 }  # ✅ 限制递归深度
```

---

## 六、工具链推荐

### 6.1 官方工具
| 工具 | 功能 | 命令 |
|------|------|------|
| `agentic validate` | 语法与契约检查 | `agentic validate *.agent.md` |
| `agentic simulate` | 本地模拟执行 | `agentic simulate --mock-llm` |
| `agentic visualize` | DAG 可视化 | `agentic visualize file.agent.md` |
| `agentic trace` | 查看执行轨迹 | `agentic trace last` |

### 6.2 IDE 支持
- **VS Code 插件**：语法高亮、自动补全、错误检查
- **Jupyter 扩展**：在 Notebook 中嵌入 `.agent.md` 块并执行

### 6.3 CI/CD 集成
```yaml
# .github/workflows/agent-test.yml
- name: Validate AgenticDSL
  run: agentic validate agents/*.agent.md

- name: Simulate Critical Paths
  run: agentic simulate agents/critical.agent.md --mode prod
```

---

## 七、最佳实践清单

### ✅ Do
- 使用 `/lib/reasoning/**` 实现推理逻辑，而非拼接原语
- 为所有 `/lib/**` 子图声明完整 `signature`
- 在开发阶段使用 `mode: dev`，上线前切换为 `prod`
- 利用 `array_concat` 处理并行结果聚合
- 通过 `archive_to` 沉淀成功经验

### ❌ Don’t
- 在叶子节点中编码高层逻辑
- 使用 `generate_subgraph` 调用已有子图
- 在生产模式下使用 `last_write_wins`
- 忽略 `permissions` 声明
- 输出非 `### AgenticDSL` 块的内容

---

## 八、附录：Core SDK 快速参考

| 路径 | 输入 | 输出 | 用途 |
|------|------|------|------|
| `/lib/reasoning/assert` | `condition: bool` | - | 中间验证 |
| `/lib/human/clarify_intent` | `prompt: string` | `clarified: string` | 意图澄清 |
| `/lib/human/approval` | `request: string` | `approved: bool, comment: string` | 人工审批 |
| `/lib/workflow/parallel_map` | `items: array, subgraph: path` | `results: array` | 并行映射 |

> 完整清单见 [AgenticDSL v3.1 规范附录 C](#)

---

> **记住：你不是在写代码，而是在设计思维的结构。**  
> AgenticDSL 让你将 LLM 的创造力约束在可验证、可进化的工程框架中——  
> 这正是构建可靠智能体的未来之路。

**© 2025 AgenticDSL Working Group. All rights reserved.**  
*本指南采用 CC BY-SA 4.0 许可，欢迎社区共建。*
