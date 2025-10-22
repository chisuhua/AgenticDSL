// tests/test_v11_no_llm.cpp
#define CATCH_CONFIG_MAIN
#include "catch_amalgamated.hpp"
#include "agenticdsl/core/engine.h"
#include "agenticdsl/tools/registry.h"
#include "agenticdsl/common/utils.h"
#include <string>

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
# --- BEGIN AgenticDSL ---
graph_type: subgraph
entry: start
nodes:
  - id: start
    type: start
    next: [prepare]
  - id: prepare
    type: assign
    assign:
      num1: "15"
      num2: "27"
    next: [compute]
  - id: compute
    type: tool_call
    tool: calculate
    arguments:
      a: "{{ num1 }}"
      b: "{{ num2 }}"
      op: "+"
    output_keys: "sum_result"
    next: [end]
  - id: end
    type: end
# --- END AgenticDSL ---
)";

    auto engine = AgenticDSLEngine::from_markdown(markdown);
    Context ctx;
    auto result = engine->run(ctx);

    REQUIRE(result.success == true);
    REQUIRE(result.final_context.contains("sum_result"));
    REQUIRE(result.final_context["sum_result"]["result"] == 42);
}

TEST_CASE("Execute Assign with Inja Functions", "[templates][assign]") {
    std::string markdown = R"(
### AgenticDSL `/main`
# --- BEGIN AgenticDSL ---
graph_type: subgraph
entry: start
nodes:
  - id: start
    type: start
    next: [process]
  - id: process
    type: assign
    assign:
      user_greeting: "Hello, {{ default(user.name, 'Guest') }}!"
      item_count: "{{ length(items) }}"
      is_empty: "{{ not exists(items) or length(items) == 0 }}"
    next: [end]
  - id: end
    type: end
# --- END AgenticDSL ---
)";

    auto engine = AgenticDSLEngine::from_markdown(markdown);
    Context ctx;
    ctx["user"]["name"] = "Alice";
    ctx["items"] = nlohmann::json::array({"apple", "banana"});

    auto result = engine->run(ctx);
    REQUIRE(result.success == true);
    REQUIRE(result.final_context["user_greeting"] == "Hello, Alice!");
    REQUIRE(result.final_context["item_count"] == 2);
    REQUIRE(result.final_context["is_empty"] == false);
}

TEST_CASE("Resource Node Injection", "[resources]") {
    std::string markdown = R"(
### AgenticDSL `/resources/config`
# --- BEGIN AgenticDSL ---
type: resource
resource_type: file
uri: "/app/config.json"
scope: global
# --- END AgenticDSL ---

### AgenticDSL `/main`
# --- BEGIN AgenticDSL ---
graph_type: subgraph
entry: start
nodes:
  - id: start
    type: start
    next: [use_resource]
  - id: use_resource
    type: assign
    assign:
      config_path: "{{ resources.config.uri }}"
    next: [end]
  - id: end
    type: end
# --- END AgenticDSL ---
)";

    auto engine = AgenticDSLEngine::from_markdown(markdown);
    Context ctx;
    auto result = engine->run(ctx);

    REQUIRE(result.success == true);
    REQUIRE(result.final_context["config_path"] == "/app/config.json");
}
