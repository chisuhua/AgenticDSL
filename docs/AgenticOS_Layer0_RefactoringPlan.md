# AgenticOS Layer 0: agentic-dsl-runtime 重构实施计划（详细）

**文档版本：** v2.2.0  
**日期：** 2026-02-25  
**范围：** agentic-dsl-runtime 重构实施 – 完整五阶段计划  
**关联文档：** [Layer 0 规范总览](AgenticOS_Layer0_Spec.md)  
**基础代码：** `chisuhua/AgenticDSL`（`src/` 目录）

> 本文档从 [`AgenticOS_Layer0_Spec.md`](AgenticOS_Layer0_Spec.md) 第 14 节独立提取，包含五个重构阶段的完整代码级实施细节：接口修改、代码差异、新增文件列表和测试用例。

---

### 总体原则

1. **最小化破坏性变更**：优先扩展接口而不是替换，保持现有测试通过
2. **增量迁移**：每个 Phase 独立可测试、可回滚
3. **现有测试先行**：每个 Phase 开始前确认 `AGENTICDSL_BUILD_TESTS=ON` 下所有现有测试通过

### Phase 总览

| Phase | 名称 | 关键任务 | 新增文件 | 预估工作量 |
| :--- | :--- | :--- | :--- | :--- |
| **Phase 1** | 代码整理与接口规范化 | 移除全局指针、去单例化、compile() 接口、结构化错误 | `src/core/types/errors.h` | 1-2 周 |
| **Phase 2** | 多 LLM 后端支持 | ILLMProvider 接口、OpenAI/Anthropic 适配器、工厂 | `illm_provider.h`, `openai_adapter.h/cpp`, `anthropic_adapter.h/cpp`, `llm_provider_factory.h/cpp` | 2-3 周 |
| **Phase 3** | 标准库分层重组 | lib/ 目录重组、StandardLibraryLoader 增强 | `lib/cognitive/`, `lib/thinking/`, `lib/workflow/` | 1-2 周 |
| **Phase 4** | Python 绑定 | pybind11 集成、Thin Wrapper 实现、pyproject.toml | `src/modules/bindings/python.cpp`, `pyproject.toml` | 1-2 周 |
| **Phase 5** | 智能化演进（v2.2） | SemanticValidator、智能调度、自适应预算、state 工具、人机协作 | `semantic_validator.h/cpp`, `adaptive_budget.h/cpp` | 3-4 周 |

---

### Phase 1：代码整理与接口规范化

**目标：** 消除全局状态，统一接口，为后续 Phase 奠定基础。

#### 1.1 移除 `g_current_llm_adapter` 全局指针（P0）

**问题根源（`src/core/engine.cpp`，第 140-143 行）：**

```cpp
// 当前问题代码
LlamaAdapter* prev = g_current_llm_adapter;
g_current_llm_adapter = llama_adapter_.get();  // ← 全局状态写入
auto result = scheduler.execute(context);
g_current_llm_adapter = prev;                   // ← 恢复（不支持并发）
```

**重构方案：** 将 `llm_adapter` 通过 `TopoScheduler::Config` 传入，让调度器直接持有指针（目前已通过构造函数参数传递，但引擎层仍设置全局变量）。

**修改 `src/common/llm/llama_adapter.h`：**

```cpp
// 删除以下行
extern LlamaAdapter* g_current_llm_adapter;
```

**修改 `src/common/llm/llama_adapter.cpp`：**

```cpp
// 删除以下行
LlamaAdapter* g_current_llm_adapter = nullptr;
```

**修改 `src/core/engine.cpp` 的 `run()` 方法：**

```cpp
// 重构后：移除全局指针操作
ExecutionResult DSLEngine::run(const Context& context) {
    std::optional<ExecutionBudget> budget;
    for (auto& g : full_graphs_) {
        if (g.budget.has_value()) {
            budget = std::move(g.budget);
            break;
        }
    }

    TopoScheduler::Config config;
    config.initial_budget = std::move(budget);
    // llama_adapter_ 已通过构造函数参数传入 TopoScheduler，无需全局指针
    TopoScheduler scheduler(std::move(config), tool_registry_,
                            llama_adapter_.get(), &full_graphs_);

    auto sys_nodes = create_system_nodes();
    for (auto& node : sys_nodes) scheduler.register_node(std::move(node));
    for (const auto& graph : full_graphs_)
        for (const auto& node : graph.nodes)
            if (node) scheduler.register_node(node->clone());
    scheduler.build_dag();

    // 移除：g_current_llm_adapter 的设置/恢复
    auto result = scheduler.execute(context);
    last_traces_ = scheduler.get_last_traces();
    return result;
}
```

**影响范围检查：** 搜索代码库中所有引用 `g_current_llm_adapter` 的位置，确认仅存在于 `llama_adapter.h/cpp` 和 `engine.cpp`，移除后不影响其他代码。

---

#### 1.2 `StandardLibraryLoader` 去单例化（P1）

**问题根源（`src/modules/library/library_loader.cpp`，第 10-18 行）：**

```cpp
// 当前：全局单例，难以在测试中隔离
StandardLibraryLoader& StandardLibraryLoader::instance() {
    static StandardLibraryLoader loader;
    static bool initialized = false;
    if (!initialized) {
        loader.load_builtin_libraries();
        initialized = true;
    }
    return loader;
}
```

**重构方案：** 保留 `instance()` 作为便利方法（向后兼容），但同时支持非单例构造，在测试和 L0 重构中使用非单例实例。

**修改 `src/modules/library/library_loader.h`：**

```cpp
class StandardLibraryLoader {
public:
    // 新增：非单例构造函数（用于测试和依赖注入）
    explicit StandardLibraryLoader(bool load_builtins = true);

    // 保留：便利单例（向后兼容）
    static StandardLibraryLoader& instance();

    const std::vector<LibraryEntry>& get_available_libraries() const;
    void load_from_directory(const std::string& lib_dir);
    void load_builtin_libraries();

private:
    std::vector<LibraryEntry> libraries_;
    MarkdownParser parser_;
};
```

**修改 `src/modules/library/library_loader.cpp`：**

```cpp
// 新增：带参数的构造函数
StandardLibraryLoader::StandardLibraryLoader(bool load_builtins) {
    if (load_builtins) {
        load_builtin_libraries();
    }
}

// 保留：单例（现在调用带参构造）
StandardLibraryLoader& StandardLibraryLoader::instance() {
    static StandardLibraryLoader loader(true); // load_builtins = true
    return loader;
}
```

**测试验证：** 在现有测试 `test_library_loader.cpp` 中增加：

```cpp
TEST_CASE("StandardLibraryLoader non-singleton", "[library]") {
    // 非单例实例，空库（不加载内置）
    StandardLibraryLoader loader(false);
    REQUIRE(loader.get_available_libraries().empty());

    // 手动加载内置
    loader.load_builtin_libraries();
    REQUIRE_FALSE(loader.get_available_libraries().empty());
}
```

---

#### 1.3 增加 `DSLEngine::compile()` 纯函数接口（P1）

**目标：** 将 DSL 解析（`from_markdown`）与执行（`run`）在接口层面更清晰分离，供 L2 单独调用解析阶段。

**修改 `src/core/engine.h`：**

```cpp
class DSLEngine {
public:
    // 现有接口（保留）
    static std::unique_ptr<DSLEngine> from_markdown(const std::string& markdown_content);
    static std::unique_ptr<DSLEngine> from_file(const std::string& file_path);
    ExecutionResult run(const Context& context = Context{});

    // 新增：仅编译，不执行（纯函数，供 L2 独立调用）
    static std::vector<ParsedGraph> compile(const std::string& markdown_content);

    // 新增：从已编译的 ParsedGraph 创建引擎（L2 用）
    static std::unique_ptr<DSLEngine> from_graphs(std::vector<ParsedGraph> graphs);

    // 保留其他现有方法...
};
```

**修改 `src/core/engine.cpp`：**

```cpp
// 新增：compile() - 纯解析，不加载 LLM
std::vector<ParsedGraph> DSLEngine::compile(const std::string& markdown_content) {
    MarkdownParser parser;
    auto graphs = parser.parse_from_string(markdown_content);
    bool has_main = false;
    for (const auto& g : graphs) {
        if (g.path == "/main") { has_main = true; break; }
    }
    if (!has_main) {
        throw std::runtime_error("Required /main subgraph not found");
    }
    return graphs;
}

// 新增：from_graphs() - 从已解析图创建引擎
std::unique_ptr<DSLEngine> DSLEngine::from_graphs(std::vector<ParsedGraph> graphs) {
    auto config = load_llm_config();
    auto engine = std::make_unique<DSLEngine>(std::move(graphs));
    engine->llama_adapter_ = std::make_unique<LlamaAdapter>(config);
    return engine;
}
```

---

#### 1.4 统一错误处理机制（P1）

**问题：** 当前错误通过 `throw std::runtime_error` 抛出，错误信息字符串不一致（如 `execute_assert` 将跳转目标编码在消息字符串中，由调度器解析）。

**重构方案：** 引入结构化错误类型：

```cpp
// 新增：src/core/types/errors.h
namespace agenticdsl {

enum class ErrorCode {
    ASSERT_FAILED,
    BUDGET_EXCEEDED,
    TOOL_NOT_FOUND,
    TOOL_PERMISSION_DENIED,
    LLM_NOT_AVAILABLE,
    NODE_NOT_FOUND,
    CYCLE_DETECTED,
    SIGNATURE_VALIDATION_FAILED,
    LAYER_PROFILE_VIOLATION,    // v2.2 新增
    STATE_TOOL_NOT_REGISTERED,  // v2.2 新增
};

struct DSLError : public std::exception {
    ErrorCode code;
    std::string message;
    std::optional<NodePath> jump_target; // 用于 assert 跳转

    DSLError(ErrorCode c, std::string msg, std::optional<NodePath> jump = std::nullopt)
        : code(c), message(std::move(msg)), jump_target(std::move(jump)) {}

    const char* what() const noexcept override { return message.c_str(); }
};

} // namespace agenticdsl
```

**修改 `execute_assert` 中的错误抛出（`src/modules/executor/node_executor.cpp`）：**

```cpp
// 重构前（编码跳转路径为字符串）
throw std::runtime_error("Assert failed. Jumping to: " + node->on_failure.value());

// 重构后（结构化错误）
throw DSLError(ErrorCode::ASSERT_FAILED,
               "Assert failed at node: " + node->path,
               node->on_failure);
```

**修改 `topo_scheduler.cpp` 中的错误处理：**

```cpp
// 重构前（解析字符串）
if (session_result.message.find("Jumping to:") != std::string::npos) {
    size_t pos = session_result.message.find("Jumping to:");
    NodePath target = session_result.message.substr(pos + 12);
    // ...
}

// 重构后（捕获结构化错误）
// 在 ExecutionSession::execute_node 中捕获 DSLError，
// 将 jump_target 返回到 ExecutionResult
struct ExecutionResult {
    Context new_context;
    bool success;
    std::string message;
    std::optional<NodePath> snapshot_key;
    std::optional<NodePath> paused_at;
    std::optional<NodePath> jump_target; // 新增
};
```

---

### Phase 2：多 LLM 后端支持

**目标：** 提取 `ILLMProvider` 接口，支持 OpenAI、Anthropic、本地 llama.cpp 多后端，通过 `llm_config.json` 配置切换。

#### 2.1 `ILLMProvider` 接口定义

**新增 `src/common/llm/illm_provider.h`：**

```cpp
#pragma once
#include <string>
#include <memory>

namespace agenticdsl {

class ILLMProvider {
public:
    virtual ~ILLMProvider() = default;

    // 生成文本（阻塞调用）
    virtual std::string generate(const std::string& prompt) = 0;

    // 检查是否已加载/可用
    virtual bool is_loaded() const = 0;

    // 后端名称（用于日志和配置）
    virtual std::string backend_name() const = 0;
};

} // namespace agenticdsl
```

#### 2.2 重构 `LlamaAdapter`

**修改 `src/common/llm/llama_adapter.h`：**

```cpp
#include "illm_provider.h"

namespace agenticdsl {

class LlamaAdapter : public ILLMProvider {  // 新增继承
public:
    struct Config {
        std::string model_path;
        int n_ctx = 2048;
        int n_threads = 4;
        float temperature = 0.7f;
        float min_p = 0.05f;
        int n_predict = 512;
    };

    explicit LlamaAdapter(const Config& config);
    ~LlamaAdapter();

    std::string generate(const std::string& prompt) override;
    bool is_loaded() const override;
    std::string backend_name() const override { return "llama.cpp"; }

    // ... 私有成员不变 ...
};

// 删除全局指针声明：
// extern LlamaAdapter* g_current_llm_adapter;  ← 删除

} // namespace agenticdsl
```

#### 2.3 新增 `OpenAIAdapter`

**新增 `src/common/llm/openai_adapter.h`：**

```cpp
#pragma once
#include "illm_provider.h"
#include <string>

namespace agenticdsl {

class OpenAIAdapter : public ILLMProvider {
public:
    struct Config {
        std::string api_key;
        std::string base_url = "https://api.openai.com/v1";
        std::string model = "gpt-4o";
        float temperature = 0.7f;
        int max_tokens = 2048;
        int timeout_sec = 30;
    };

    explicit OpenAIAdapter(const Config& config);

    std::string generate(const std::string& prompt) override;
    bool is_loaded() const override { return !config_.api_key.empty(); }
    std::string backend_name() const override { return "openai"; }

private:
    Config config_;
    // HTTP 调用实现（可选：libcurl 或内置 HTTP 客户端）
    std::string http_post(const std::string& url, const std::string& body);
};

} // namespace agenticdsl
```

**`OpenAIAdapter::generate` 实现（`src/common/llm/openai_adapter.cpp`）：**

```cpp
std::string OpenAIAdapter::generate(const std::string& prompt) {
    // 构建 OpenAI Chat Completion 请求体
    nlohmann::json request = {
        {"model", config_.model},
        {"messages", {{{"role", "user"}, {"content", prompt}}}},
        {"temperature", config_.temperature},
        {"max_tokens", config_.max_tokens}
    };

    std::string response_body = http_post(
        config_.base_url + "/chat/completions",
        request.dump()
    );

    // 解析响应
    auto resp_json = nlohmann::json::parse(response_body);
    return resp_json["choices"][0]["message"]["content"].get<std::string>();
}
```

#### 2.4 `LLMProviderFactory`

**新增 `src/common/llm/llm_provider_factory.h`：**

```cpp
#pragma once
#include "illm_provider.h"
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

namespace agenticdsl {

class LLMProviderFactory {
public:
    // 从 llm_config.json 创建适配器
    // 配置格式：{ "backend": "llama|openai|anthropic", ... }
    static std::unique_ptr<ILLMProvider> create_from_config(
        const std::string& config_path = "llm_config.json");

    // 直接从 JSON 对象创建
    static std::unique_ptr<ILLMProvider> create_from_json(const nlohmann::json& config);
};

} // namespace agenticdsl
```

**`llm_config.json` 配置示例（扩展当前格式）：**

```json
{
  "backend": "openai",
  "openai": {
    "api_key": "${OPENAI_API_KEY}",
    "model": "gpt-4o",
    "temperature": 0.7,
    "max_tokens": 2048
  },
  "llama": {
    "model_path": "models/qwen-0.6b.gguf",
    "n_ctx": 2048,
    "n_threads": 4
  }
}
```

#### 2.5 修改 `DSLEngine` 使用 `ILLMProvider`

**修改 `src/core/engine.h`：**

```cpp
#include "common/llm/illm_provider.h"  // 替换 llama_adapter.h

class DSLEngine {
    // ...
private:
    std::unique_ptr<ILLMProvider> llm_provider_;  // 替换 llama_adapter_
    // ...
};
```

**修改 `src/core/engine.cpp`：**

```cpp
std::unique_ptr<DSLEngine> DSLEngine::from_markdown(const std::string& markdown_content) {
    MarkdownParser parser;
    auto graphs = parser.parse_from_string(markdown_content);
    // ...
    auto engine = std::make_unique<DSLEngine>(std::move(graphs));
    engine->llm_provider_ = LLMProviderFactory::create_from_config(); // 工厂创建
    return engine;
}
```

**测试验证（新增 `tests/test_llm_provider.cpp`）：**

```cpp
TEST_CASE("LLMProviderFactory selects llama backend", "[llm]") {
    nlohmann::json config = {{"backend", "llama"}, {"llama", {{"model_path", "none"}}}};
    // 仅测试工厂选择逻辑，不实际加载模型
    // 验证返回 LlamaAdapter 实例
}

TEST_CASE("OpenAIAdapter config parsing", "[llm]") {
    nlohmann::json config = {
        {"backend", "openai"},
        {"openai", {{"api_key", "test-key"}, {"model", "gpt-4o"}}}
    };
    auto provider = LLMProviderFactory::create_from_json(config);
    REQUIRE(provider->backend_name() == "openai");
    REQUIRE(provider->is_loaded() == true); // api_key is set
}
```

---

### Phase 3：标准库分层重组

**目标：** 将 `lib/` 目录按 AgenticOS 层级重组，增强 `StandardLibraryLoader` 支持层级感知加载和签名验证。

#### 3.1 目录迁移计划

**现有 `lib/` 结构：**
```text
lib/
├── auth/       → 迁移到 lib/workflow/auth/
├── human/      → 迁移到 lib/workflow/human/
├── math/       → 迁移到 lib/workflow/math/（工具函数，Workflow Profile）
└── utils/      → 迁移到 lib/utils/（跨层通用）
```

**目标结构：**
```text
lib/
├── cognitive/                  # L4 认知层（Cognitive Profile）
│   ├── route_decision.md       # 路由决策子图
│   └── confidence_eval.md      # 置信度评估子图
├── thinking/                   # L3 推理层（Thinking Profile）
│   ├── react_loop.md           # ReAct 循环子图
│   └── with_rollback.md        # 现有 /lib/reasoning/with_rollback
├── workflow/                   # L2 工作流层（Workflow Profile）
│   ├── auth/                   # 认证工具（从 lib/auth/ 迁移）
│   ├── human/                  # 人机交互（从 lib/human/ 迁移）
│   └── math/                   # 数学计算（从 lib/math/ 迁移）
└── utils/                      # 通用工具（跨层）
    └── noop.md
```

#### 3.2 DSL 文件中的 Layer Profile 声明

每个标准库 DSL 文件需在 `/__meta__` 块中声明 `layer_profile`：

**示例：`lib/cognitive/route_decision.md`**
```markdown
### AgenticDSL `/__meta__`
```yaml
# --- BEGIN AgenticDSL ---
layer_profile: Cognitive
version: "1.0"
# --- END AgenticDSL ---
```

### AgenticDSL `/lib/cognitive/route_decision`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
signature: "(context: object, options: array) -> {chosen_path: string}"
permissions: ["state:read"]
nodes:
  - id: start
    type: start
    next: ["/lib/cognitive/route_decision/decide"]
  - id: decide
    type: llm_call
    prompt_template: "Given context {{ context }}, choose from {{ options }}..."
    output_keys: ["chosen_path"]
    next: ["/lib/cognitive/route_decision/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
```

#### 3.3 增强 `StandardLibraryLoader`

**修改 `src/modules/library/library_loader.h`：**

```cpp
// 新增字段
struct LibraryEntry {
    NodePath path;
    std::optional<std::string> signature;
    std::optional<nlohmann::json> output_schema;
    std::vector<std::string> permissions;
    bool is_subgraph = true;
    std::string layer_profile = "Workflow"; // 新增：Cognitive/Thinking/Workflow
};

class StandardLibraryLoader {
public:
    explicit StandardLibraryLoader(bool load_builtins = true);
    static StandardLibraryLoader& instance();

    // 新增：按层加载
    void load_layer(const std::string& lib_dir, const std::string& layer_profile);

    // 新增：按 Profile 查询
    std::vector<LibraryEntry> get_libraries_for_profile(
        const std::string& profile) const;

    // 新增：签名验证
    bool validate_signature(const NodePath& lib_path,
                            const nlohmann::json& input) const;

    const std::vector<LibraryEntry>& get_available_libraries() const;
    void load_from_directory(const std::string& lib_dir);
    void load_builtin_libraries();
};
```

**修改 `src/modules/library/library_loader.cpp`：**

```cpp
void StandardLibraryLoader::load_layer(const std::string& lib_dir,
                                        const std::string& layer_profile) {
    namespace fs = std::filesystem;
    if (!fs::exists(lib_dir) || !fs::is_directory(lib_dir)) return;

    for (const auto& entry : fs::recursive_directory_iterator(lib_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".md") continue;

        std::ifstream file(entry.path());
        std::stringstream buffer;
        buffer << file.rdbuf();
        try {
            auto graphs = parser_.parse_from_string(buffer.str());
            for (const auto& g : graphs) {
                if (g.is_standard_library) {
                    LibraryEntry e;
                    e.path = g.path;
                    e.signature = g.signature;
                    e.output_schema = g.output_schema;
                    e.permissions = g.permissions;
                    e.is_subgraph = true;
                    e.layer_profile = layer_profile; // 标记层级
                    libraries_.push_back(std::move(e));
                }
            }
        } catch (...) { /* 跳过加载失败的文件 */ }
    }
}

void StandardLibraryLoader::load_builtin_libraries() {
    // 按层加载（Phase 3 后使用分层目录）
    load_layer("lib/cognitive", "Cognitive");
    load_layer("lib/thinking", "Thinking");
    load_layer("lib/workflow", "Workflow");
    load_layer("lib/utils", "Workflow"); // 通用工具默认 Workflow Profile

    // 保留内置硬编码条目（向后兼容）
    libraries_.push_back({
        "/lib/math/add",
        "(a: number, b: number) -> {sum: number}",
        nlohmann::json::parse(R"({"type":"object","properties":{"sum":{"type":"number"}}})"),
        {},
        true,
        "Workflow"
    });
}
```

---

### Phase 4：pybind11 Python 绑定

**目标：** 通过 pybind11 暴露最小化 C++ 接口，Python 层不含任何业务逻辑。

#### 4.1 CMakeLists.txt 修改

在 `CMakeLists.txt` 中增加 pybind11 支持：

```cmake
# 在 cmake_minimum_required 之后增加
option(AGENTICDSL_BUILD_PYTHON "Build Python bindings" OFF)

if(AGENTICDSL_BUILD_PYTHON)
    find_package(pybind11 REQUIRED)

    pybind11_add_module(agentic_dsl_runtime
        src/modules/bindings/python.cpp
    )

    target_link_libraries(agentic_dsl_runtime PRIVATE
        agenticdsl_core
        ${LLAMA_LIB}
        yaml-cpp
    )

    # 设置输出目录（方便 pip install）
    set_target_properties(agentic_dsl_runtime PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/python/agentic_dsl_runtime"
    )
endif()
```

#### 4.2 pybind11 绑定实现

**新增 `src/modules/bindings/python.cpp`：**

```cpp
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include "core/engine.h"
#include "core/types/budget.h"
#include "core/types/node.h"

namespace py = pybind11;
using namespace agenticdsl;

// JSON <-> Python dict 转换辅助
nlohmann::json py_dict_to_json(const py::dict& d) {
    return nlohmann::json::parse(py::module_::import("json")
        .attr("dumps")(d).cast<std::string>());
}

py::dict json_to_py_dict(const nlohmann::json& j) {
    return py::module_::import("json")
        .attr("loads")(j.dump()).cast<py::dict>();
}

PYBIND11_MODULE(agentic_dsl_runtime, m) {
    m.doc() = "AgenticDSL Runtime - L0 C++ core engine (Thin Python Wrapper)";

    // ExecutionResult
    py::class_<ExecutionResult>(m, "ExecutionResult")
        .def_property_readonly("success", [](const ExecutionResult& r) { return r.success; })
        .def_property_readonly("message", [](const ExecutionResult& r) { return r.message; })
        .def_property_readonly("final_context",
            [](const ExecutionResult& r) { return json_to_py_dict(r.final_context); })
        .def_property_readonly("paused_at",
            [](const ExecutionResult& r) -> py::object {
                if (r.paused_at) return py::str(*r.paused_at);
                return py::none();
            });

    // TraceRecord
    py::class_<TraceRecord>(m, "TraceRecord")
        .def_property_readonly("node_path", [](const TraceRecord& t) { return t.node_path; })
        .def_property_readonly("status", [](const TraceRecord& t) { return t.status; })
        .def_property_readonly("context_delta",
            [](const TraceRecord& t) { return json_to_py_dict(t.context_delta); })
        .def_property_readonly("budget_snapshot",
            [](const TraceRecord& t) { return json_to_py_dict(t.budget_snapshot); });

    // DSLEngine - Thin Wrapper，不暴露内部实现细节
    py::class_<DSLEngine>(m, "DSLEngine")
        .def_static("from_markdown", &DSLEngine::from_markdown,
            py::arg("markdown_content"),
            "Parse and compile a DSL string")
        .def_static("from_file", &DSLEngine::from_file,
            py::arg("file_path"),
            "Parse and compile a DSL file")
        .def("run",
            [](DSLEngine& self, const py::dict& context) {
                py::gil_scoped_release release; // 释放 GIL（C++ 执行不阻塞 Python）
                return self.run(py_dict_to_json(context));
            },
            py::arg("context") = py::dict{},
            "Execute the compiled DSL workflow")
        .def("register_tool",
            [](DSLEngine& self, const std::string& name, py::function fn) {
                self.register_tool(name,
                    [fn](const std::unordered_map<std::string, std::string>& args)
                    -> nlohmann::json {
                        py::gil_scoped_acquire acquire; // 调用 Python 函数时重新获取 GIL
                        py::dict py_args;
                        for (const auto& [k, v] : args) py_args[k.c_str()] = v;
                        py::object result = fn(py_args);
                        // 支持返回 dict、str、int、bool
                        if (py::isinstance<py::dict>(result)) {
                            return py_dict_to_json(result.cast<py::dict>());
                        }
                        return nlohmann::json(result.cast<std::string>());
                    });
            },
            py::arg("name"), py::arg("handler"),
            "Register a Python function as a DSL tool")
        .def("get_last_traces", &DSLEngine::get_last_traces,
            "Get execution traces from the last run");

    // 版本信息
    m.attr("__version__") = "0.1.0";
    m.attr("__runtime_version__") = "2.2.0";
}
```

#### 4.3 pyproject.toml

**新增 `pyproject.toml`：**

```toml
[build-system]
requires = ["scikit-build-core>=0.4", "pybind11>=2.11"]
build-backend = "scikit_build_core.build"

[project]
name = "agentic-dsl-runtime"
version = "0.1.0"
description = "AgenticOS Layer 0 DSL Runtime - C++ core engine Python bindings"
requires-python = ">=3.10"
license = {text = "MIT"}

[tool.scikit-build]
cmake.args = ["-DAGENTICDSL_BUILD_PYTHON=ON"]
cmake.build-type = "Release"

[project.optional-dependencies]
dev = ["pytest>=7.0"]
```

#### 4.4 Python 绑定测试

**新增 `tests/test_bindings.py`：**

```python
"""测试 pybind11 Thin Wrapper 接口（不包含业务逻辑）"""
import pytest

try:
    import agentic_dsl_runtime as runtime
    HAS_RUNTIME = True
except ImportError:
    HAS_RUNTIME = False

SIMPLE_DSL = """
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/assign"]
  - id: assign
    type: assign
    assign:
      result: "hello from dsl"
    next: ["/main/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
"""

@pytest.mark.skipif(not HAS_RUNTIME, reason="C++ runtime not built")
def test_from_markdown_and_run():
    engine = runtime.DSLEngine.from_markdown(SIMPLE_DSL)
    result = engine.run({})
    assert result.success
    assert result.final_context["result"] == "hello from dsl"

@pytest.mark.skipif(not HAS_RUNTIME, reason="C++ runtime not built")
def test_register_python_tool():
    engine = runtime.DSLEngine.from_markdown("""
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/call_tool"]
  - id: call_tool
    type: tool_call
    tool: py_tool
    arguments: {x: "42"}
    output_keys: ["result"]
    next: ["/main/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
""")
    engine.register_tool("py_tool", lambda args: {"result": int(args["x"]) * 2})
    result = engine.run({})
    assert result.success
    assert result.final_context["result"] == 84

@pytest.mark.skipif(not HAS_RUNTIME, reason="C++ runtime not built")
def test_execution_traces():
    engine = runtime.DSLEngine.from_markdown(SIMPLE_DSL)
    engine.run({})
    traces = engine.get_last_traces()
    assert len(traces) > 0
    assert any(t.node_path == "/main/start" for t in traces)
```

---

### Phase 5：智能化演进特性（v2.2 核心）

**目标：** 实现 AgenticOS v2.2 的核心智能化特性：Layer Profile 编译时验证、智能调度、自适应预算、state 工具路由、风险感知人机协作。

#### 5.1 `SemanticValidator`（Layer Profile 编译时验证）

**新增 `src/modules/parser/semantic_validator.h`：**

```cpp
#pragma once
#include "modules/parser/markdown_parser.h"
#include "core/types/node.h"
#include <vector>
#include <string>

namespace agenticdsl {

class SemanticValidator {
public:
    explicit SemanticValidator(const std::vector<ParsedGraph>& graphs);

    // 运行所有验证，失败抛出 DSLError
    void validate();

private:
    const std::vector<ParsedGraph>& graphs_;

    // 验证子图命名空间与 layer_profile 匹配
    // /lib/cognitive/** → Cognitive
    // /lib/thinking/**  → Thinking
    // /lib/workflow/**  → Workflow
    void validate_namespace_profile_mapping();

    // 验证 Cognitive Profile 下 tool_call 只允许 state.read/state.write
    void validate_cognitive_tool_restrictions();

    // 验证 Thinking Profile 下禁止 state.write
    void validate_thinking_tool_restrictions();

    // 验证 state.read/write 工具在 __meta__/resources 中声明
    void validate_state_tool_declarations();

    // 验证所有 next/wait_for 引用的节点存在
    void validate_node_references();

    // 帮助方法：从节点路径推断所在子图的 layer_profile
    std::string infer_layer_profile(const NodePath& path) const;
};

} // namespace agenticdsl
```

**`SemanticValidator` 核心实现（`src/modules/parser/semantic_validator.cpp`）：**

```cpp
void SemanticValidator::validate_namespace_profile_mapping() {
    // 图路径 → 期望 Profile 的映射规则
    static const std::vector<std::pair<std::string, std::string>> rules = {
        {"/lib/cognitive/", "Cognitive"},
        {"/lib/thinking/",  "Thinking"},
        {"/lib/workflow/",  "Workflow"},
    };

    for (const auto& graph : graphs_) {
        for (const auto& [prefix, expected_profile] : rules) {
            if (graph.path.rfind(prefix, 0) == 0) {
                // 图路径匹配前缀，检查 /__meta__ 中的 layer_profile 声明
                std::string declared_profile = "Workflow"; // 默认
                for (const auto& g : graphs_) {
                    if (g.path == "/__meta__" && g.metadata.contains("layer_profile")) {
                        declared_profile = g.metadata["layer_profile"].get<std::string>();
                    }
                }
                if (declared_profile != expected_profile) {
                    throw DSLError(
                        ErrorCode::LAYER_PROFILE_VIOLATION,
                        "Graph " + graph.path + " requires " + expected_profile +
                        " profile, but declared " + declared_profile
                    );
                }
                break;
            }
        }
    }
}

void SemanticValidator::validate_cognitive_tool_restrictions() {
    for (const auto& graph : graphs_) {
        if (graph.path.rfind("/lib/cognitive/", 0) != 0) continue;
        for (const auto& node : graph.nodes) {
            if (node->type != NodeType::TOOL_CALL) continue;
            const auto* tool_node = static_cast<const ToolCallNode*>(node.get());
            if (tool_node->tool_name != "state.read" &&
                tool_node->tool_name != "state.write") {
                throw DSLError(
                    ErrorCode::LAYER_PROFILE_VIOLATION,
                    "Cognitive Profile node " + node->path +
                    " cannot call tool '" + tool_node->tool_name +
                    "' (only state.read/state.write allowed)"
                );
            }
        }
    }
}

void SemanticValidator::validate_thinking_tool_restrictions() {
    for (const auto& graph : graphs_) {
        if (graph.path.rfind("/lib/thinking/", 0) != 0) continue;
        for (const auto& node : graph.nodes) {
            if (node->type != NodeType::TOOL_CALL) continue;
            const auto* tool_node = static_cast<const ToolCallNode*>(node.get());
            if (tool_node->tool_name == "state.write") {
                throw DSLError(
                    ErrorCode::LAYER_PROFILE_VIOLATION,
                    "Thinking Profile node " + node->path +
                    " cannot use state.write (read-only access)"
                );
            }
        }
    }
}
```

**集成到 `DSLEngine::compile()`（Phase 1 新增的纯函数接口）：**

```cpp
std::vector<ParsedGraph> DSLEngine::compile(const std::string& markdown_content) {
    MarkdownParser parser;
    auto graphs = parser.parse_from_string(markdown_content);

    // Phase 5 新增：语义验证
    SemanticValidator validator(graphs);
    validator.validate(); // 编译时检查，失败抛出 DSLError

    bool has_main = false;
    for (const auto& g : graphs) {
        if (g.path == "/main") { has_main = true; break; }
    }
    if (!has_main) throw std::runtime_error("Required /main subgraph not found");
    return graphs;
}
```

**Layer Profile 验证测试（新增 `tests/test_layer_profile.cpp`）：**

```cpp
#include "catch_amalgamated.hpp"
#include "modules/parser/semantic_validator.h"
#include "modules/parser/markdown_parser.h"

TEST_CASE("Cognitive Profile rejects non-state tool calls", "[layer_profile]") {
    std::string dsl = R"(
### AgenticDSL `/__meta__`
```yaml
# --- BEGIN AgenticDSL ---
layer_profile: Cognitive
# --- END AgenticDSL ---
```

### AgenticDSL `/lib/cognitive/bad_node`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/lib/cognitive/bad_node/call"]
  - id: call
    type: tool_call
    tool: web_search  # ← 违规：Cognitive Profile 不允许
    arguments: {query: "test"}
    output_keys: ["result"]
    next: ["/lib/cognitive/bad_node/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
)";
    agenticdsl::MarkdownParser parser;
    auto graphs = parser.parse_from_string(dsl);
    agenticdsl::SemanticValidator validator(graphs);
    REQUIRE_THROWS_AS(validator.validate(), agenticdsl::DSLError);
}

TEST_CASE("Thinking Profile rejects state.write", "[layer_profile]") {
    // ... 类似测试结构 ...
}

TEST_CASE("Workflow Profile allows any tool", "[layer_profile]") {
    // ... 验证无违规 ...
}
```

---

#### 5.2 智能调度（`metadata.priority`）

**问题：** 当前 `TopoScheduler` 使用 `std::queue<NodePath>`（FIFO），无法按优先级排序。

**修改 `src/modules/scheduler/topo_scheduler.h`：**

```cpp
#include <queue>
#include <functional>

namespace agenticdsl {

// 新增：带优先级的节点队列元素
struct PriorityNode {
    NodePath path;
    int priority;

    // 最大堆：priority 大的先出队
    bool operator<(const PriorityNode& other) const {
        return priority < other.priority;
    }
};

class TopoScheduler {
    // ...
private:
    // 修改：将 std::queue<NodePath> 替换为优先级队列
    std::priority_queue<PriorityNode> ready_queue_; // ← 替换原来的 std::queue

    // 新增：从节点 metadata 读取优先级
    int get_node_priority(const Node* node) const {
        if (node->metadata.contains("priority") &&
            node->metadata["priority"].is_number_integer()) {
            return node->metadata["priority"].get<int>();
        }
        return 0; // 默认优先级
    }
};

} // namespace agenticdsl
```

**修改 `src/modules/scheduler/topo_scheduler.cpp` 中所有 `ready_queue_` 操作：**

```cpp
// build_dag() 中：
// 修改前：
ready_queue_.push(path);
// 修改后：
ready_queue_.push({path, get_node_priority(node_ptr.get())});

// execute() 中取队首：
// 修改前：
current_path = ready_queue_.front();
ready_queue_.pop();
// 修改后：
current_path = ready_queue_.top().path;
ready_queue_.pop();

// 后继节点入队（in_degree 归零时）：
// 修改前：
ready_queue_.push(next_path);
// 修改后：
Node* next_node = node_map_.at(next_path);
ready_queue_.push({next_path, get_node_priority(next_node)});
```

**DSL 使用示例：**

```yaml
### AgenticDSL `/main/critical_check`
```yaml
# --- BEGIN AgenticDSL ---
type: tool_call
tool: health_check
metadata:
  priority: 10        # 高优先级，相同依赖层中优先执行
  critical_path: true
arguments: {}
output_keys: ["health_status"]
next: ["/main/proceed"]
# --- END AgenticDSL ---
```

---

#### 5.3 自适应预算（`AdaptiveBudgetCalculator`）

**新增 `src/modules/budget/adaptive_budget.h`：**

```cpp
#pragma once
#include "core/types/budget.h"
#include "core/types/context.h"

namespace agenticdsl {

class AdaptiveBudgetCalculator {
public:
    // 基于置信度分数计算子图预算比例
    // ratio = 0.3 + 0.4 * confidence_score  (range: [0.3, 0.7])
    // 例：confidence=0.9 → ratio=0.66，confidence=0.3 → ratio=0.42
    static float compute_ratio(float confidence_score);

    // 从父预算和置信度创建子图预算
    // 子图 max_nodes = parent.max_nodes * ratio
    // 性能要求：< 1ms
    static ExecutionBudget create_child_budget(
        const ExecutionBudget& parent,
        float confidence_score);

    // 从 Context 中推断置信度分数（读取 context["confidence_score"]）
    // 如果不存在，返回默认值 0.5
    static float extract_confidence(const Context& ctx);
};

} // namespace agenticdsl
```

**实现（`src/modules/budget/adaptive_budget.cpp`）：**

```cpp
float AdaptiveBudgetCalculator::compute_ratio(float confidence_score) {
    // 限制到 [0.0, 1.0]
    confidence_score = std::clamp(confidence_score, 0.0f, 1.0f);
    // 线性映射到 [0.3, 0.7]
    return 0.3f + 0.4f * confidence_score;
}

ExecutionBudget AdaptiveBudgetCalculator::create_child_budget(
    const ExecutionBudget& parent, float confidence_score) {

    float ratio = compute_ratio(confidence_score);
    ExecutionBudget child;

    // 按比例分配，-1 表示无限制（继承无限制）
    if (parent.max_nodes >= 0) {
        child.max_nodes = static_cast<int>(parent.max_nodes * ratio);
        child.max_nodes = std::max(child.max_nodes, 1); // 至少 1 个节点
    } else {
        child.max_nodes = -1;
    }

    if (parent.max_llm_calls >= 0) {
        child.max_llm_calls = static_cast<int>(parent.max_llm_calls * ratio);
        child.max_llm_calls = std::max(child.max_llm_calls, 1);
    } else {
        child.max_llm_calls = -1;
    }

    if (parent.max_duration_sec >= 0) {
        child.max_duration_sec = static_cast<int>(parent.max_duration_sec * ratio);
        child.max_duration_sec = std::max(child.max_duration_sec, 1);
    } else {
        child.max_duration_sec = -1;
    }

    child.max_subgraph_depth = (parent.max_subgraph_depth >= 0)
        ? parent.max_subgraph_depth - 1  // 深度递减
        : -1;

    return child;
}

float AdaptiveBudgetCalculator::extract_confidence(const Context& ctx) {
    if (ctx.contains("confidence_score") && ctx["confidence_score"].is_number()) {
        return ctx["confidence_score"].get<float>();
    }
    return 0.5f; // 默认置信度
}
```

**集成到 `GenerateSubgraphNode` 执行（修改 `execute_generate_subgraph`）：**

```cpp
// 在 execute_generate_subgraph 中，为动态子图创建自适应预算
Context NodeExecutor::execute_generate_subgraph(
    const GenerateSubgraphNode* node, const Context& ctx) {

    // ... 现有 LLM 调用和解析逻辑 ...

    // Phase 5 新增：从上下文提取置信度，创建子图自适应预算
    float confidence = AdaptiveBudgetCalculator::extract_confidence(ctx);
    // 子图预算从 ExecutionSession 的当前预算计算（需传入引用）
    // 暂存到 context 中供调度器使用
    new_context["__child_budget_confidence__"] = confidence;

    // ... 其余逻辑 ...
}
```

**测试（新增 `tests/test_adaptive_budget.cpp`）：**

```cpp
#include "catch_amalgamated.hpp"
#include "modules/budget/adaptive_budget.h"

TEST_CASE("AdaptiveBudgetCalculator compute_ratio", "[budget][adaptive]") {
    REQUIRE(agenticdsl::AdaptiveBudgetCalculator::compute_ratio(0.0f) == Catch::Approx(0.3f));
    REQUIRE(agenticdsl::AdaptiveBudgetCalculator::compute_ratio(1.0f) == Catch::Approx(0.7f));
    REQUIRE(agenticdsl::AdaptiveBudgetCalculator::compute_ratio(0.5f) == Catch::Approx(0.5f));
    // 边界值：超出 [0,1] 被 clamp
    REQUIRE(agenticdsl::AdaptiveBudgetCalculator::compute_ratio(-0.5f) == Catch::Approx(0.3f));
    REQUIRE(agenticdsl::AdaptiveBudgetCalculator::compute_ratio(1.5f) == Catch::Approx(0.7f));
}

TEST_CASE("AdaptiveBudgetCalculator create_child_budget", "[budget][adaptive]") {
    agenticdsl::ExecutionBudget parent;
    parent.max_nodes = 50;
    parent.max_llm_calls = 10;
    parent.max_duration_sec = 60;
    parent.max_subgraph_depth = 3;

    auto child = agenticdsl::AdaptiveBudgetCalculator::create_child_budget(parent, 0.5f);

    REQUIRE(child.max_nodes == 25);       // 50 * 0.5 = 25
    REQUIRE(child.max_llm_calls == 5);    // 10 * 0.5 = 5
    REQUIRE(child.max_duration_sec == 30);// 60 * 0.5 = 30
    REQUIRE(child.max_subgraph_depth == 2);// 3 - 1 = 2
}

TEST_CASE("AdaptiveBudgetCalculator extract_confidence from context", "[budget][adaptive]") {
    agenticdsl::Context ctx;
    ctx["confidence_score"] = 0.8f;
    REQUIRE(agenticdsl::AdaptiveBudgetCalculator::extract_confidence(ctx) == Catch::Approx(0.8f));

    agenticdsl::Context empty_ctx;
    REQUIRE(agenticdsl::AdaptiveBudgetCalculator::extract_confidence(empty_ctx) == Catch::Approx(0.5f));
}
```

---

#### 5.4 `state.read`/`state.write` 工具路由

**目标：** `NodeExecutor::execute_tool_call` 对 `state.read`/`state.write` 工具进行特殊路由，通过 `ToolRegistry` 调用（L2 注册的实现）并在运行时验证权限。

**修改 `src/modules/executor/node_executor.cpp`，增加路由逻辑：**

```cpp
Context NodeExecutor::execute_tool_call(const ToolCallNode* node, const Context& ctx) {
    Context new_context = ctx;
    const auto& tool_name = node->tool_name;

    // state 工具：验证权限声明
    if (tool_name == "state.read" || tool_name == "state.write") {
        // 检查节点是否声明了对应权限
        std::string required_perm = "state:" + tool_name; // e.g., "state:state.read"
        bool has_perm = std::find(node->permissions.begin(),
                                  node->permissions.end(),
                                  required_perm) != node->permissions.end();
        if (!has_perm) {
            throw DSLError(
                ErrorCode::TOOL_PERMISSION_DENIED,
                "Node " + node->path + " requires permission '" + required_perm +
                "' to use " + tool_name
            );
        }

        // 验证工具已注册（L2 必须提前注册）
        if (!tool_registry_.has_tool(tool_name)) {
            throw DSLError(
                ErrorCode::STATE_TOOL_NOT_REGISTERED,
                tool_name + " is not registered. L2 must register it via register_tool()."
            );
        }
    }

    // 渲染参数（Inja 模板）
    std::unordered_map<std::string, std::string> rendered_args;
    for (const auto& [key, tmpl] : node->arguments) {
        rendered_args[key] = InjaTemplateRenderer::render(tmpl, ctx);
    }

    // 调用工具（state.read/write 走同一 ToolRegistry，L2 注入实现）
    if (!tool_registry_.has_tool(tool_name)) {
        throw DSLError(ErrorCode::TOOL_NOT_FOUND,
                       "Tool '" + tool_name + "' not registered");
    }
    nlohmann::json result = tool_registry_.call_tool(tool_name, rendered_args);

    // 处理 output_keys
    if (node->output_keys.size() == 1) {
        new_context[node->output_keys[0]] = result;
    } else if (result.is_object()) {
        for (const auto& key : node->output_keys) {
            if (result.contains(key)) new_context[key] = result[key];
        }
    } else if (!node->output_keys.empty()) {
        new_context[node->output_keys[0]] = result;
    }

    return new_context;
}
```

**DSL 使用示例（`state.read`）：**

```yaml
### AgenticDSL `/lib/cognitive/read_user_profile`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
signature: "(user_id: string) -> {profile: object}"
permissions: ["state:state.read"]
nodes:
  - id: start
    type: start
    next: ["/lib/cognitive/read_user_profile/read"]
  - id: read
    type: tool_call
    tool: state.read
    permissions: ["state:state.read"]
    arguments:
      key: "user_profile:{{ user_id }}"
    output_keys: ["profile"]
    next: ["/lib/cognitive/read_user_profile/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```

**L2 中注册 state 工具（宿主程序示例，参考 `examples/agent_basic/main.cpp`）：**

```cpp
auto engine = agenticdsl::DSLEngine::from_file("workflow.md");

// L2 注入 state 工具实现（CognitiveStateManager 来自 AgenticOS L4）
engine->register_tool("state.read", [&state_manager](const auto& args) {
    std::string key = args.at("key");
    return state_manager.read(key); // 返回 JSON
});

engine->register_tool("state.write", [&state_manager](const auto& args) {
    std::string key = args.at("key");
    std::string value = args.at("value");
    state_manager.write(key, value);
    return nlohmann::json{{"success", true}};
});
```

**测试（新增 `tests/test_state_tool.cpp`）：**

```cpp
#include "catch_amalgamated.hpp"
#include "core/engine.h"

TEST_CASE("state.read tool registered and callable", "[state_tool]") {
    std::string dsl = R"(
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/read_state"]
  - id: read_state
    type: tool_call
    tool: state.read
    permissions: ["state:state.read"]
    arguments: {key: "test_key"}
    output_keys: ["value"]
    next: ["/main/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
)";
    auto engine = agenticdsl::DSLEngine::from_markdown(dsl);
    // 注册 mock state.read
    engine->register_tool("state.read", [](const auto& args) -> nlohmann::json {
        return {{"value", "mock_state_value"}};
    });

    auto result = engine->run({});
    REQUIRE(result.success);
    REQUIRE(result.final_context["value"] == nlohmann::json{{"value", "mock_state_value"}});
}

TEST_CASE("state.read without permission declaration throws", "[state_tool]") {
    std::string dsl = R"(
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/read_state"]
  - id: read_state
    type: tool_call
    tool: state.read
    # permissions: 未声明 → 应抛出权限错误
    arguments: {key: "test_key"}
    output_keys: ["value"]
    next: ["/main/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
)";
    auto engine = agenticdsl::DSLEngine::from_markdown(dsl);
    engine->register_tool("state.read", [](const auto&) -> nlohmann::json {
        return {{"value", "x"}};
    });
    auto result = engine->run({});
    REQUIRE_FALSE(result.success); // 权限检查失败
}
```

---

#### 5.5 风险感知人机协作（`require_human_approval: risk_based`）

**目标：** 当节点 `metadata.risk_level` 超过阈值时，暂停执行并通知宿主程序（L2）进行人工审批。

**设计：** 利用现有的 `ExecutionResult::paused_at` 字段（已被 LLM 调用暂停使用），扩展为支持人工审批暂停。

**修改 `src/core/types/budget.h`（扩展 `ExecutionResult`）：**

```cpp
struct ExecutionResult {
    bool success;
    std::string message;
    Context final_context;
    std::optional<NodePath> paused_at; // LLM 暂停 或 人工审批暂停
    enum class PauseReason { NONE, LLM_CALL, HUMAN_APPROVAL } pause_reason = PauseReason::NONE;
    float risk_score = 0.0f; // 暂停时的风险分数
};
```

**在 `ExecutionSession::execute_node` 中，LLM_CALL 节点执行前检查风险：**

```cpp
// Phase 5 新增：风险感知人机协作检查
if (current_node->metadata.contains("risk_level")) {
    float risk = current_node->metadata["risk_level"].get<float>();
    float threshold = 0.7f; // 默认阈值（可从 /__meta__ 配置）

    if (risk >= threshold) {
        // require_human_approval: risk_based → 暂停执行
        return {context, false,
                "Human approval required for high-risk node: " + current_node->path,
                std::nullopt, std::nullopt};
        // paused_at 由调度器设置，pause_reason = HUMAN_APPROVAL
    }
}
```

**DSL 使用示例：**

```yaml
### AgenticDSL `/main/delete_user`
```yaml
# --- BEGIN AgenticDSL ---
type: tool_call
tool: db_delete
metadata:
  risk_level: 0.9        # 高风险操作
  description: "不可逆：删除用户数据"
permissions: ["db:write"]
arguments:
  user_id: "{{ user_id }}"
output_keys: ["deleted"]
next: ["/main/end"]
# --- END AgenticDSL ---
```

**宿主程序（L2）处理暂停：**

```cpp
auto result = engine->run(context);
while (result.paused_at.has_value() &&
       result.pause_reason == ExecutionResult::PauseReason::HUMAN_APPROVAL) {
    std::cout << "Human approval required for: " << *result.paused_at
              << " (risk=" << result.risk_score << ")" << std::endl;

    // 等待人工确认（通过 UI/API）
    bool approved = wait_for_human_decision(*result.paused_at);
    if (!approved) {
        // 人工拒绝，终止执行
        break;
    }
    // 人工批准，继续执行
    result = engine->continue_from(*result.paused_at, result.final_context);
}
```

---
---

**文档结束**  
**关联文档：** [AgenticOS Layer 0 规范总览](AgenticOS_Layer0_Spec.md) | [AgenticOS 架构总纲](AgenticOS_Architecture.md)  
**基于代码库：** `chisuhua/AgenticDSL`（`src/` 目录，2026-02-25）
