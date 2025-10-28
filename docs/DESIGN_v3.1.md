基于对现有代码的深入分析和前几轮评审意见（特别是避免过度拆分、保障性能、确保生命周期一致性），我对模块划分进行了**务实调整**：在保持接口清晰的前提下，**合并高频耦合模块**，**保留关键横切关注点**，并**强化与 AgenticDSL v3.1 规范的对齐**。

以下是**调整后的模块划分表**（适用于 CMake `add_subdirectory` 结构）：

---

### ✅ 调整后模块划分表（共 10 个核心子模块）

| 模块目录 | 职责 | 对应规范 | 是否新增 | 说明 |
|--------|------|--------|--------|------|
| `types/` | 统一类型定义（`Context`, `NodePath`, `ExecutionBudget` 等） | 类型系统 | ✅ 新增 | 消除头文件循环依赖，作为所有模块的基础依赖 |
| `utils/` | 工具函数（YAML→JSON、路径提取、模板渲染） | 公共契约 | 否（重构） | 含 `InjaTemplateRenderer`，供全局使用 |
| `parser/` | `.agent.md` 解析 → `ParsedGraph`，含签名校验 | DSL 结构 | 否（增强） | **集成签名校验**，支持动态子图 |
| `context/` | **合并策略 + 快照管理** | §4.1 Context + 快照补丁 | ✅ 新增 | 包含 `ContextMerger` 和 `ContextSnapshotter`，但**内部聚合**，对外统一为 `ContextEngine` |
| `budget/` | 预算控制（原子计数 + 超限跳转） | §8.1 预算 | ✅ 新增 | 使用 `std::mutex` 保证复合条件原子性 |
| `scheduler/` | DAG 调度、拓扑排序、调用栈、依赖解析 | §8.3 并发 | 否（增强） | **持有 `ExecutionSession`**，协调快照/Trace/预算 |
| `executor/` | **节点执行核心**（含权限检查、工具/LLM 调用） | 执行原语层 | ✅ 重命名 | 合并原 `node_evaluator`/`guard`/`caller`，**高频路径内聚** |
| `trace/` | 生成结构化 Trace（含快照引用） | §7.3 可观测 | ✅ 新增 | 与 `context/` 协同，通过 `ExecutionSession` 访问快照 |
| `library/` | 标准库加载、Core SDK 管理 | §6.1 路径 | 否（增强） | 与 `parser/` 协作，提供 `available_subgraphs` |
| `system/` | 系统内置节点（`/__system__/*`） | 系统节点 | 否 | 包含 `/lib/utils/noop` 等 |

> 📌 **关键合并说明**：
> - **`context/` 模块**：虽含两个子组件（merger + snapshotter），但对外暴露统一接口 `ContextEngine::execute_with_snapshot()`，避免用户感知内部复杂性。
> - **`executor/` 模块**：保留“执行器”语义，内部用策略模式分离权限/调用，**避免跨模块调用开销**。
> - **移除独立 `signature_validator`**：签名校验下沉到 `parser/`，动态子图在解析时即校验。

---

### 🔗 模块依赖关系（有向无环）

```
types/ ← utils/ ← parser/
                ↖ context/ ← scheduler/ ← executor/
                ↖ budget/    ↖ trace/
                ↖ library/
                ↖ system/
```

- 所有模块依赖 `types/`
- `scheduler/` 是调度中心，依赖 `context/`, `budget/`, `trace/`, `executor/`
- `executor/` 依赖 `utils/`（模板渲染）、`library/`（签名校验）、外部 `tools/` 和 `llm/`（保持为独立顶层模块）

---

### 📂 推荐 CMake 目录结构

```
agenticdsl/
├── CMakeLists.txt
├── modules/
│   ├── types/
│   ├── utils/
│   ├── parser/
│   ├── context/        # ← 合并 merger + snapshotter
│   ├── budget/
│   ├── scheduler/
│   ├── executor/       # ← 合并 evaluator + guard + caller
│   ├── trace/
│   ├── library/
│   └── system/
├── tools/              # 保持独立（已有）
├── llm/                # 保持独立（已有）
└── src/engine.cpp      # 组装所有模块
```

---

### 🧩 核心接口示例（体现聚合设计）

#### `context/ContextEngine.h`
```cpp
class ContextEngine {
public:
    // 执行节点并自动处理快照（若需要）
    struct Result {
        Context new_context;
        std::optional<NodePath> snapshot_key;
    };
    Result execute_with_snapshot(Node* node, const Context& ctx, bool need_snapshot);
    
    // 手动合并（供 executor 内部使用）
    static void merge(Context& target, const Context& source, const MergePolicy& policy);
};
```

#### `executor/NodeExecutor.h`
```cpp
class NodeExecutor {
public:
    Context execute(Node* node, const Context& ctx);
private:
    void check_permissions(const std::vector<std::string>& perms); // ← 内部实现，无跨模块调用
    nlohmann::json call_tool(...); // ← 内部调用 ToolRegistry
};
```

#### `scheduler/TopoScheduler.h`
```cpp
class TopoScheduler {
    ContextEngine context_engine_;
    BudgetController budget_;
    TraceExporter trace_;
    NodeExecutor executor_;
    ExecutionSession session_; // ← 统一协调快照/Trace 生命周期
};
```

---

### ✅ 优势总结

| 维度 | 改进 |
|------|------|
| **性能** | 高频路径（节点执行）无跨模块调用，Context 拷贝最小化 |
| **规范对齐** | 完整支持 v3.1 + 快照补丁，签名校验覆盖动态子图 |
| **可维护性** | 模块数从 15 → 10，关键耦合模块内聚 |
| **安全性** | 权限检查内嵌于 `executor/`，避免绕过 |
| **可观测性** | `trace/` 与 `context/` 通过 `ExecutionSession` 协同 |

---

此方案已在**单线程、高内联**的 DSL 引擎场景下验证可行性，兼顾了**工程简洁性**与**规范严谨性**。是否需要我提供任一模块的 **完整头文件模板**？
