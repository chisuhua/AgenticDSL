# AgenticDSL v3.9 应用开发指南（修订版）  
**构建可验证、可进化的智能体应用 —— 通用 C++ 原子能力 + 静态/动态图驱动**

> **最后更新**：2025年11月10日  
> **适用对象**：Agentic 系统开发者、C++ 工程师、智能体架构师  
> **核心理念**：  
> - **原子能力** 由 C++ 模块实现（通过 `tool_call` 调用）  
> - **应用逻辑** 由 AgenticDSL 静态图编排（`/app/<AppName>/**`）  
> - **可复用组件** 必须发布至 `/lib/workflow/**` 并带 `signature`  
> - **持续优化** 由动态图实现（`/dynamic/**` + `archive_to`）  
> - **运行时确定性**：单线程调度，禁止异步回调（v3.9 §1.3）

---

## 一、适用场景与复杂度映射

| 应用类型 | 推荐模式 | C++ 模块职责 |
|----------|--------|------------|
| **简单控制**（传感器、电机） | 单 `tool_call` + `output_mapping` | 原子 I/O，**幂等设计**（见 2.7） |
| **中等逻辑**（规划、校验） | `/app/<AppName>/**` 静态图 | 提供 `plan`, `verify` 工具 |
| **复杂系统**（推理、多模态） | C++ 提供原子工具，DSL 编排高层策略 | `tokenize`, `kv_alloc`, `model_step` |
| **自适应系统**（机器人、风控） | 静态图 + 动态图 + `archive_to` | 所有原子能力 + **TTL 协同**（见 3.3） |

> ✅ **所有场景均遵循三层架构**（v3.9 §2.1）：  
> - **执行原语层**：`assign`, `tool_call`, `assert`（不可扩展）  
> - **标准原语层**：`/lib/**`（带 `signature`，全局可复用）  
> - **知识应用层**：  
>   - **私有逻辑** → `/app/<AppName>/**`  
>   - **可复用组件** → `/lib/workflow/**` 或 `/lib/knowledge/**`

---

## 二、C++ 模块开发：原子能力封装

### 2.1 项目结构（推荐）
```text
my_app/
├── CMakeLists.txt
├── include/agentic_native/tool_interface.hpp
├── src/tools/
│   ├── my_action.cpp
│   └── register_tools.cpp
└── app/my_app/main.agent.md   # ← 入口 DAG
```

### 2.2 工具接口规范（符合 v3.9 §2.2 + §10.3.6）

```cpp
// tool_interface.hpp
#pragma once
#include <functional>
#include <vector>
#include <string>

namespace agentic {
class JsonValue;
struct ToolSchema {
  std::vector<std::pair<std::string, std::string>> inputs;
  std::vector<std::pair<std::string, std::string>> outputs;
  std::vector<Permission> required_permissions; // 结构化权限
};

using ToolFunc = std::function<JsonValue(const JsonValue& args)>;
class ToolRegistry {
public:
  void registerTool(const std::string& name, ToolFunc impl, ToolSchema schema);
};
}
```

### 2.2.1 输入/输出类型白名单（v3.9 §4.2）

所有 `inputs` 和 `outputs` 的类型字段必须为以下之一：

| 类型标识 | JSON 类型 | 说明 |
|--------|----------|------|
| `"string"` | string | UTF-8 字符串 |
| `"number"` | number | **双精度浮点**（整数也应表示为 1.0） |
| `"boolean"` | boolean | true / false |
| `"object"` | object | **仅扁平键值对**（值限上述三种） |
| `"array_string"` | array | 元素全为 string |
| `"array_number"` | array | 元素全为 number |

> ⚠️ **禁止**：
> - `"integer"`（应统一为 `"number"`）
> - 嵌套对象（如 `{"user": {"id": "u1"}}`）
> - 混合数组（如 `[1, "a"]`）

### 2.3 工具实现示例

```cpp
agentic::JsonValue motor_control(const agentic::JsonValue& args) {
  int speed = static_cast<int>(args["speed"].get<double>());
  std::string dir = args["direction"].get<std::string>();
  std::string idempotency_key = args["idempotency_key"].get<std::string>("");

  // ✅ 幂等性：重复 idempotency_key 返回相同结果
  if (cache.has(idempotency_key)) {
    return cache.get(idempotency_key);
  }

  // ✅ 协作式超时：定期检查 is_cancelled()
  if (agentic::is_cancelled()) {
    throw ToolError("ERR_TOOL_CANCELLED");
  }

  Hardware::setMotor(speed, dir);

  // ✅ 返回 { "result": ... } + meta（v3.9 §10.3.6）
  auto result = JsonValue::object({{"status", "ok"}});
  cache.set(idempotency_key, result);
  return JsonValue::object({
    {"result", result},
    {"meta", JsonValue::object({
      {"latency_ms", 2},
      {"backend", "motor_driver_v1"}
    })}
  });
}
```

### 2.4 工具注册（使用结构化权限）

```cpp
void registerMyAppTools(agentic::ToolRegistry& reg) {
  reg.registerTool("motor_control", motor_control, {
    .inputs = {{"speed", "number"}, {"direction", "string"}, {"idempotency_key", "string"}},
    .outputs = {{"status", "string"}},
    .required_permissions = {
      agentic::Permission{.action = "control", .resource = "motor", .domain = "robot"}
    }
  });
}
```

### 2.5 运行时交互模型（关键！）

AgenticDSL **采用单线程、确定性调度循环**（v3.9 §1.3, §8.1）：

- `tool_call` 节点执行时，**执行器同步调用 C++ 函数**，阻塞等待返回；
- **C++ 模块不得启动线程、注册回调、异步通知**；
- 每个 `tool_call` 节点消耗 **1 个 `max_nodes` 预算单位**；
- 超时由执行器控制（默认 5s），超时 → `ERR_TOOL_TIMEOUT` → `on_error`；
- 上下文更新与 Trace 在返回后 **原子完成**。

> ⚠️ **禁止**：C++ 模块直接读写上下文、开启后台任务、无限循环。

### 2.6 错误处理与可观测性

- C++ 抛出异常 → 执行器捕获 → 转为 `error_context` → 跳转 `on_error`
- 错误码应标准化（如 `ERR_TOOL_TIMEOUT`, `ERR_INVALID_INPUT`）
- 返回 `meta.latency_ms`, `meta.backend` 供 Trace 使用
- 执行器将错误信息注入 `$.error` 上下文：
  - `$.error.code`：如 `"ERR_INVALID_INPUT"`
  - `$.error.message`
  - `$.error.context`：原始 `tool_call` 的 arguments
- C++ 可附加结构化上下文：
  ```cpp
  throw ToolError("ERR_EXEC.MODEL_FAIL")
      .with_context({{"model_version", "v3"}, {"input_hash", hash}});
  ```

### 2.7 幂等性与可重试性（工业/机器人场景）

> 对于网络调用、硬件控制等场景，**强烈建议支持幂等**：
> ```yaml
> arguments:
>   idempotency_key: "task_{{ $.task.id }}"
> ```
> 执行器可在 `on_error` 后自动重试，依赖工具幂等保证一致性。

### 2.8 日志与审计安全

- **禁止** 使用 `std::cout`、`printf`、`fprintf`；
- 必须通过 `agentic::log_info(...)` 等结构化接口记录；
- 敏感字段（如 user_id、device_id）需标记 PII 级别：
  ```cpp
  agentic::log_debug("motor_cmd", {
    {"device_id", device_id, agentic::PII::DEVICE_ID},
    {"speed", speed}
  });
  ```
- 执行器自动 redact PII 字段并注入 `trace_id`。

### 2.9 资源约束与中断处理

- **CPU 时间**：单次 `tool_call` ≤ **500ms**（超时抛 `ERR_TOOL_TIMEOUT`）；
- **内存峰值**：≤ **100MB**（由 sandbox 监控）；
- **协作式中断**：长时间操作需定期检查：
  ```cpp
  if (agentic::is_cancelled()) {
    throw ToolError("ERR_EXEC.CANCELLED");
  }
  ```
- **禁止**：大块堆分配、无限循环、阻塞 I/O 无超时。

### 2.10 运行时配置注入

所有可变参数必须通过配置系统读取：
```cpp
int default_speed = agentic::get_config<int>("robot.default_speed", 50);
```

**配置优先级**（高→低）：
1. DSL `arguments`（覆盖）
2. 环境变量（`ROBOT_DEFAULT_SPEED=60`）
3. 配置文件（`/etc/agents/config.yaml`）
4. 默认值

> ⚠️ **禁止**：硬编码业务阈值、端点 URL、模型版本。

### 2.11 依赖与构建规范

- **依赖声明**：必须通过 `CMakeLists.txt` 显式管理（推荐 `FetchContent`）；
- **禁止**：链接系统全局库（如 `/usr/lib/libxxx.so`）；
- **ABI 稳定**：导出函数必须为 `extern "C"`；
- **构建产物**：生成无前缀 `.so`（如 `motor_agent.so`，非 `libmotor_agent.so`）；
- **许可证**：仅允许 MIT/Apache 2.0/BSD 等宽松许可证。

---

## 三、静态图驱动：应用主流程

### 3.0 应用目录结构与命名空间语义（v3.9 新增）

AgenticDSL v3.9 引入 **应用隔离模型**，通过命名空间明确代码用途：

| 路径 | 用途 | 是否可复用 | 是否需签名 |
|------|------|-----------|----------|
| **`/app/<AppName>/**`** | **应用专属主流程（默认工作目录）** | ❌ 默认不可 | ❌ 可选（仅内部有效） |
| `/lib/workflow/**`<br>`/lib/knowledge/**` | **可复用子图库（标准库）** | ✅ 全局可用 | ✅ 强制 |
| `/main/**` | 临时脚本/原型 | ❌ | ❌ |
| `/dynamic/**` | 运行时生成 | ⚠️ 会话内有效 | ⚠️ 可选 |

> **最佳实践**：
> - 所有新项目以 `/app/<AppName>/main` 作为入口。
> - 当某个子图被多个应用需要时，应将其重构并发布到 `/lib/workflow/...`。
> - 使用 `archive_to(...)` 将验证成功的动态子图固化为 `/lib/` 组件。

### 3.1 默认路径：`/app/<AppName>/**`（v3.9 §6.1, §6.3）

- **v3.9 起，推荐所有生产应用使用 `/app/<AppName>/**` 作为默认工作目录**。
- 执行器将此目录视为**应用私有命名空间**，其中的子图不会污染全局库。
- 若需暴露能力给其他应用或 LLM 调用，必须通过以下任一方式：
  1. 手动将子图移至 `/lib/workflow/<domain>/...` 并添加 `signature`；
  2. 在成功执行后调用 `archive_to("/lib/workflow/...@v1")` 自动归档（见 4.2）。
- **`/main/**` 保留用于快速实验，但不应出现在生产部署包中**。

### 3.2 示例：机器人启动（修正命名空间与权限）

```markdown
### AgenticDSL `/__meta__`
yaml
version: "3.9"
mode: prod
entry_point: "/app/robot/main"  # ✅ 符合 v3.9 推荐格式

### AgenticDSL `/__meta__/resources`
yaml
type: resource_declare
resources:
  - type: tool
    name: motor_control
    capabilities: [speed_control]

### AgenticDSL `/app/robot/main`
yaml
type: tool_call
tool: motor_control
arguments:
  speed: 50
  direction: "forward"
  idempotency_key: "startup_{{ $.task.id }}"
output_mapping:
  status: "result.status"
permissions:
  - action: control
    resource: motor
    domain: robot
next: "/end"
```

### 3.3 C++ 与 TTL 协同（修正实现模型）

> **C++ 模块不得监听上下文过期事件**（v3.9 §5.1 明确 TTL 由执行器管理）。  
> 正确做法：**C++ 模块在每次调用时主动验证资源有效性**。
>
> ```cpp
> if (!KVCache::isValid(args["kv_handle"])) {
>   throw ToolError("ERR_KV_EXPIRED");
> }
> ```
>
> DSL 层负责生命周期（**仅 `context.persistent.*` 支持 TTL**）：
> ```yaml
> assign:
>   expr: "{{ $.new_kv_handle }}"
>   path: "context.persistent.robot.kv_{{ $.task.id }}"
>   meta:
>     ttl_seconds: 600
> ```

---

## 四、动态图优化：自进化能力

### 4.1 动态子图生成

```yaml
type: generate_subgraph  # ← /lib/dslgraph/generate@v1
prompt_template: "绕行障碍..."
signature_validation: strict
next: "/dynamic/avoid_{{ $.now }}"
```

> ⚠️ **禁止直接使用 `llm_generate_dsl`**（v3.9 §2.1, A2）

### 4.2 成功策略归档（v3.9 §8.4）

```yaml
on_success: "archive_to('/lib/workflow/robot/solved/obstacle_avoidance@v1')"
```

> **归档目标必须位于 `/lib/**`**，否则无效。  
> 归档后可作为新标准子图被其他应用复用。  
> - 版本号必须遵循 **语义化版本（SemVer）**；  
> - 归档时自动计算子图内容 SHA256，并写入 `/lib/.../manifest.json`；  
> - 调用方可通过 `signature` 字段验证完整性。

---

## 五、复杂应用模式

### 5.1 长时任务处理
- **推荐**：拆解为多步 `tool_call`（`init → step → finalize`）
- **避免**：单节点阻塞 > 5s（破坏可终止性）

### 5.2 多假设验证（v3.9 §10.2.1）
```yaml
next: "/lib/reasoning/hypothesize_and_verify@v1"
```

### 5.3 IPER 闭环（v3.9 §10.2.7）
```yaml
next: "/lib/reasoning/iper_loop@v1"
```

---

## 六、开发与部署

### 6.1 开发阶段
```bash
agentic register-tool --name motor_control --lib ./build/libmy_app.so
agentic simulate app/robot/main.agent.md --mode dev --app-name robot
```

> `--app-name robot` 用于推断默认工作目录为 `/app/robot`

### 6.2 生产部署
- 执行器通过配置自动加载 `.so`
- 必须声明 `/__meta__/resources`
- 必须设置 `mode: prod`
- **入口必须为 `/app/<AppName>/...`**

### 6.3 发布前自动化检查

所有生产模块必须通过以下 CI 流水线：
```bash
# 1. DSL 语法与路径合规
agentic-lint --dir app/ --strict

# 2. 工具契约验证
contract-validator --tool motor_control --schema ./schema.json

# 3. 安全扫描（SAST + 许可证）
sast-scan --lang cpp src/ && license-checker

# 4. 端到端模拟
agentic-simulator --dsl app/robot/main.agent.md --expect 'status:"ok"'
```

> **任一失败 → 阻断发布**。

---

## 七、合规性检查清单（v3.9 更新版）

| 要求 | 检查点 | 规范依据 |
|------|-------|----------|
| **应用隔离** | 主流程位于 `/app/<AppName>/**` | v3.9 §6.1 |
| **能力暴露** | 可复用子图位于 `/lib/workflow/**` 并含 `signature` | v3.9 §6.2, §11 |
| **入口规范** | 生产 `entry_point` 不指向 `/main/**` | v3.9 §6.3 |
| **执行原语不可扩展** | C++ 不定义新 `type` | v3.9 §5.x |
| **工具返回结构** | `{ "result": { ... } }` | v3.9 §10.3.6 |
| **输入/输出类型** | 使用白名单类型（如 `"number"`） | v3.9 §4.2 |
| **权限结构化** | `permissions` 为对象列表，非字符串 | v3.9 §7.2 |
| **动态图生成** | 调用 `/lib/dslgraph/generate@v1` | v3.9 §2.1 |
| **权限最小化** | 声明所需最小权限集 | v3.9 §7.2 |
| **确定性** | 禁止异步/线程 | v3.9 §1.3 |
| **幂等性** | 支持 `idempotency_key`（推荐） | 工业实践 |
| **上下文合并** | 生产环境禁用 `last_write_wins` | v3.9 §8.5 |
| **日志安全** | 无裸日志，PII 字段脱敏 | v3.9 §2.8 |
| **资源边界** | CPU ≤500ms，内存 ≤100MB | v3.9 §2.9 |

---

## 八、附录：C++ 模块设计反模式

### ❌ 禁止行为
- 内部状态依赖（破坏确定性）
- 启动线程或异步回调
- 直接写入上下文
- 无限循环或无超时操作
- 返回非 JSON 兼容类型
- **监听上下文 TTL 事件**
- 使用 `std::cout` 或未脱敏日志
- 硬编码配置参数
- 链接系统全局库

### ✅ 推荐行为
- 纯函数式接口
- 显式输入/输出契约（使用白名单类型）
- 主动上报性能指标
- 幂等设计（含 `idempotency_key`）
- **每次调用时验证资源有效性**
- 通过结构化接口记录日志
- 使用配置系统注入参数
- 遵循构建与依赖规范

---

## 九、总结

AgenticDSL 是 **智能体的操作系统**：

- **C++ 模块 = 驱动程序**（原子能力，通过 `tool_call` 调用）  
- **`/app/<AppName>/** = 应用沙盒**（私有主流程，默认工作目录）  
- **`/lib/workflow/** = 系统 SDK**（契约化、可复用组件）  
- **`/dynamic/** = 用户程序缓存**（运行时生成，可归档沉淀）  

通过此分层，任何复杂系统都可构建为 **可验证、可演进、可部署** 的 Agentic 应用。

> **演进路径**：  
> `/app/xxx`（开发） → 验证成功 → `archive_to(/lib/workflow/xxx@v1)` → 全局复用

**© 2025 AgenticDSL Working Group.**
