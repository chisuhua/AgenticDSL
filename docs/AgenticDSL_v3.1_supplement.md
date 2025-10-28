以下是对 **AgenticDSL v3.1 规范**的正式补丁提案，包含两个紧密关联的增强模块：

1. **Context 快照机制规范补丁（执行器层增强）**  
2. **回溯推理子图示例（推理原语层实现）**

该补丁完全兼容 v3.1 原则，**不新增执行原语节点类型**，仅通过执行器行为扩展和标准库子图实现回溯能力。

---

# AgenticDSL v3.1 补丁提案：Context 快照与回溯推理支持  
**提案编号**：PATCH-CTX-SNAPSHOT-v3.1  
**适用规范版本**：v3.1  
**状态**：社区草案（可纳入 v3.2）  

---

## 一、Context 快照机制规范补丁（执行器层）

### 1.1 新增上下文只读字段：`$.ctx_snapshots`

执行器必须在运行时维护一个**只读映射** `$.ctx_snapshots`，其结构为：

```json
{
  "ctx_snapshots": {
    "/main/step3": { /* 完整上下文快照 */ },
    "/lib/reasoning/hypothesis_test@v1": { /* 快照 */ }
  }
}
```

- **键（key）**：触发快照的**节点路径**（如 `/main/solve`）
- **值（value）**：该节点**执行前**的完整上下文副本（深拷贝）
- **访问权限**：只读。任何 `assign`、`tool_call` 等节点**不得写入** `$.ctx_snapshots`
- **生命周期**：随 DAG 执行结束自动销毁

### 1.2 快照触发策略（自动、可配置）

执行器**自动**在以下节点类型执行前保存快照（仅当 `mode: dev` 或显式启用）：

| 节点类型 | 触发条件 |
|--------|--------|
| `fork` | 总是触发（分支探索前） |
| `generate_subgraph` | 总是触发（动态生成前） |
| `assert` | 总是触发（验证前） |
| `tool_call` / `codelet_call` | **仅当声明 `rollback_on_failure: true`** |
| 其他节点 | 不触发（除非通过元指令显式请求） |

> ✅ **生产模式（`mode: prod`）默认禁用快照**，可通过 `execution_budget.enable_snapshots: true` 显式开启。

### 1.3 快照资源控制

快照受全局预算约束：

```yaml
### AgenticDSL `/__meta__`
execution_budget:
  max_snapshots: 5        # 默认：dev=10, prod=0
  snapshot_max_size_kb: 512  # 单快照最大体积（压缩后）
```

- 超出 `max_snapshots` 时，**按 FIFO 策略丢弃最早快照**
- 快照序列化必须使用紧凑 JSON（禁止格式化空格）

### 1.4 快照恢复方式（通过标准 `assign`）

用户可通过 `assign` 节点恢复快照（通常在 `on_failure` 路径中）：

```yaml
type: assign
assign:
  expr: "{{ $.ctx_snapshots['/main/step3'] }}"
  path: ""  # 全量覆盖上下文
# 或
assign:
  expr: "{{ $.ctx_snapshots['/main/step3'].user_input }}"
  path: "user_input"  # 部分恢复
```

> ⚠️ **安全限制**：表达式中对 `$.ctx_snapshots` 的访问必须为**静态字符串键**（禁止动态计算键名），防止信息泄露。

---

## 二、回溯推理子图示例（推理原语层）

以下为规范推荐的 **带回溯能力的推理原语子图**，应纳入 `/lib/reasoning/**`。

### 2.1 子图：`/lib/reasoning/with_rollback`

```markdown
### AgenticDSL `/lib/reasoning/with_rollback`
yaml
signature:
  inputs:
    - name: try_path
      type: string
      description: "主尝试路径（如 '/dynamic/solve_attempt'）"
      required: true
    - name: fallback_path
      type: string
      description: "回溯后执行路径"
      required: true
    - name: checkpoint_node
      type: string
      description: "用于恢复的快照节点路径（默认为本节点路径）"
      required: false
  outputs:
    - name: success
      type: boolean
      required: true
  version: "1.0"
  stability: stable

# 自动触发快照（因是 assert 类节点）
type: assert
condition: "true"  # 无条件通过，仅用于触发快照
on_failure: "/self/never_called"  # 占位

next: "{{ $.try_path }}"

### AgenticDSL `/lib/reasoning/with_rollback/fallback`
yaml
# 此节点在 try_path 失败后由调用者跳转至此
type: assign
assign:
  expr: "{{ $.ctx_snapshots['{{ $.checkpoint_node or \"/lib/reasoning/with_rollback\" }}'] }}"
  path: ""  # 恢复上下文
next: "{{ $.fallback_path }}"
```

#### 使用示例：
```yaml
### AgenticDSL `/main/task`
type: generate_subgraph
prompt_template: "尝试解方程 {{ $.expr }}"
next: "/lib/reasoning/with_rollback@v1"

### AgenticDSL `/lib/reasoning/with_rollback@v1`
# 自动保存快照 at /lib/reasoning/with_rollback
# 执行 /dynamic/solve_attempt
# 若失败，跳转至 /main/task/on_failure → 调用 fallback 子图
```

---

### 2.2 子图：`/lib/reasoning/hypothesis_branch`

支持多假设并行探索，失败分支自动回退：

```markdown
### AgenticDSL `/lib/reasoning/hypothesis_branch`
yaml
signature:
  inputs:
    - name: hypotheses
      type: array
      items: { type: string }  # 子图路径列表
      required: true
  outputs:
    - name: selected_hypothesis
      type: string
      required: true
  version: "1.0"
  stability: experimental

type: fork
fork:
  branches: "{{ $.hypotheses }}"
context_merge_policy:
  "hypothesis_result": error_on_conflict  # 仅允许一个成功
on_failure: "/self/rollback_all"

### AgenticDSL `/lib/reasoning/hypothesis_branch/rollback_all`
yaml
# 清理所有分支写入，恢复到 fork 前状态
type: assign
assign:
  expr: "{{ $.ctx_snapshots['/lib/reasoning/hypothesis_branch'] }}"
  path: ""
next: "/self/fallback_strategy"
```

---

## 三、Trace 增强（可观测性）

在 `mode: dev` 下，Trace 必须包含快照信息（若存在）：

```json
{
  "node_path": "/main/solve",
  "ctx_snapshot_available": true,
  "ctx_snapshot_key": "/main/solve",
  "context_snapshot": { ... }  // 可选，若 budget 允许
}
```

---

## 四、安全与兼容性说明

- **向后兼容**：未使用快照的 DAG 行为完全不变
- **权限控制**：`ctx_snapshots` 仅在子图内部可见，跨子图不可访问（沙箱隔离）
- **性能保障**：生产环境默认关闭，避免内存膨胀
- **LLM 透明**：LLM 无需理解快照机制，只需调用标准回溯子图

---

## 五、更新附录 C：核心标准库清单（v3.1+）

| 路径 | 用途 | 稳定性 |
|------|------|--------|
| `/lib/reasoning/with_rollback` | **带回溯的通用尝试-修复模式** | stable |
| `/lib/reasoning/hypothesis_branch` | 多假设并行探索 | experimental |
| （其余保持不变） | | |

---

> ✅ **此补丁使 AgenticDSL 具备工业级回溯能力，同时严格遵守“执行原语层不可扩展”原则。**  
> 回溯逻辑完全封装在推理原语层，执行器仅提供轻量、安全的状态快照基础设施。

---

**提案人**：AgenticDSL 社区  
**评审建议**：建议在 v3.2 中作为可选特性引入，标记为 `experimental`，经 3 个月生产验证后提升为 `stable`。
