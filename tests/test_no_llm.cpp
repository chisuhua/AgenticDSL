// tests/test_no_llm.cpp
#include "catch_amalgamated.hpp"
#include "agenticdsl/core/engine.h"
#include "agenticdsl/tools/registry.h"
#include "common/utils.h"
#include <string>
#include <iostream>

using namespace agenticdsl;

// Register mock tools before tests
static struct ToolRegistrar {
    ToolRegistrar() {
        ToolRegistry::instance().register_tool("calculate", [](const std::unordered_map<std::string, std::string>& args) -> nlohmann::json {
            double a = std::stod(args.at("a"));
            double b = std::stod(args.at("b"));
            std::string op = args.at("op");
            double result = (op == "+") ? a + b : a - b;
            return nlohmann::json{{"result", result}};
        });
    }
} registrar;

TEST_CASE("Execute Assign + ToolCall Workflow", "[engine][executor]") {
    std::string markdown = R"(
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/prepare"]
  - id: prepare
    type: assign
    assign:
      num1: "15"
      num2: "27"
    next: ["/main/compute"]
  - id: compute
    type: tool_call
    tool: calculate
    arguments:
      a: "{{ num1 }}"
      b: "{{ num2 }}"
      op: "+"
    output_keys: "sum_result"
    next: ["/main/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
)";

    auto engine = DSLEngine::from_markdown(markdown);
    Context ctx;
    auto result = engine->run(ctx);

    REQUIRE(result.success == true);
    REQUIRE(result.final_context.contains("sum_result"));
    REQUIRE(result.final_context["sum_result"]["result"] == 42);
}

TEST_CASE("Execute Assign with Inja Functions", "[templates][assign]") {
    std::string markdown = R"(
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/process"]
  - id: process
    type: assign
    assign:
      user_greeting: "Hello, {{ default(user.name, \"Guest\") }}!"
      item_count: "{{ length(items) }}"
      is_empty: "{{ not exists(\"items\") or length(items) == 0 }}"
    next: ["/main/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
)";

    auto engine = DSLEngine::from_markdown(markdown);
    Context ctx;
    ctx["user"]["name"] = "Alice";
    ctx["items"] = nlohmann::json::array({"apple", "banana"});

    auto result = engine->run(ctx);
    REQUIRE(result.success == true);
    REQUIRE(result.final_context["user_greeting"] == "Hello, Alice!");
    REQUIRE(result.final_context["item_count"] == "2");
    REQUIRE(result.final_context["is_empty"] == "false");
}

TEST_CASE("Resource Node Injection", "[resources]") {
    std::string markdown = R"(
### AgenticDSL `/resources/config`
```yaml
# --- BEGIN AgenticDSL ---
type: resource
resource_type: file
uri: "/app/config.json"
scope: global
# --- END AgenticDSL ---
```

### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/use_resource"]
  - id: use_resource
    type: assign
    assign:
      config_path: "{{ at(resources, \"/resources/config\").uri }}"
    wait_for: ["/resources/config"]
    next: ["/main/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
)";

    auto engine = DSLEngine::from_markdown(markdown);
    Context ctx;
    auto result = engine->run(ctx);

    REQUIRE(result.success == true);
    REQUIRE(result.final_context["config_path"] == "/app/config.json");
}
