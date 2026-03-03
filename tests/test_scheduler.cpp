// tests/test_scheduler.cpp
#include "catch_amalgamated.hpp"
#include "core/engine.h"
#include <iostream>
#include <string>

// Helper: 执行 DSL 并返回最终上下文
agenticdsl::Context run_dsl(const std::string& markdown) {
    auto engine = agenticdsl::DSLEngine::from_markdown(markdown);
    auto result = engine->run();
    REQUIRE(result.success);
    return result.final_context;
}

// Test 1: Basic DAG with linear flow (sanity check)
TEST_CASE("Linear DAG Execution", "[stage1][scheduler]") {
    std::string markdown = R"(
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
entry: start
nodes:
  - id: start
    type: assign
    assign:
      step1: "done"
    next: /main/step2
  - id: step2
    type: assign
    assign:
      step2: "done"
    next: /main/end
  - id: end
    type: end
    termination_mode: hard
# --- END AgenticDSL ---
```
)";

    auto ctx = run_dsl(markdown);
    REQUIRE(ctx.contains("step1"));
    REQUIRE(ctx.contains("step2"));
}

// Test 2: Concurrent branches with wait_for (all_of)
TEST_CASE("Concurrent Branches with wait_for", "[stage1][scheduler]") {
    std::string markdown = R"(
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
entry: start
nodes:
  - id: start
    type: assign
    assign:
      trigger: "go"
    next: ["/task/a", "/task/b"]
  - id: join
    type: assign
    assign:
      final: "{{ result_a }}+{{ result_b }}"
    wait_for:
      all_of: ["/task/a", "/task/b"]
    next: /main/end
  - id: end
    type: end
    termination_mode: hard
# --- END AgenticDSL ---
```

### AgenticDSL `/task/a`
```yaml
# --- BEGIN AgenticDSL ---
type: assign
assign:
  result_a: "A"
next: "/main/join"
# --- END AgenticDSL ---
```

### AgenticDSL `/task/b`
```yaml
# --- BEGIN AgenticDSL ---
type: assign
assign:
  result_b: "B"
next: "/main/join"
# --- END AgenticDSL ---
```
)";

    auto ctx = run_dsl(markdown);
    REQUIRE(ctx["final"] == "A+B");
}

// Test 3: soft termination allows parent flow to continue
TEST_CASE("Soft Termination Continues Parent Flow", "[stage1][scheduler]") {
    std::string markdown_with_dep = R"(
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
entry: start
nodes:
  - id: start
    type: assign
    assign:
      before_lib: "yes"
    next: "/__system__/noop"
  - id: after_lib
    type: assign
    assign:
      after_lib: "executed"
    wait_for: ["/__system__/noop"]
    next: /main/end
  - id: end
    type: end
    termination_mode: hard
# --- END AgenticDSL ---
```
)";

    auto ctx = run_dsl(markdown_with_dep);
    REQUIRE(ctx["before_lib"] == "yes");
    REQUIRE(ctx["after_lib"] == "executed");
}

// Test 4: System node /__system__/budget_exceeded is registered and reachable
TEST_CASE("System Node Budget Exceeded is Registered", "[stage1][scheduler]") {
    std::string markdown = R"(
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
entry: start
nodes:
  - id: start
    type: assign
    assign:
      test: "value"
    next: "/__system__/budget_exceeded"
# --- END AgenticDSL ---
```
)";

    auto ctx = run_dsl(markdown);
    // Should terminate cleanly (hard end)
    REQUIRE(ctx["test"] == "value");
}

// Test 5: /lib/utils/noop executes without error (soft end)
TEST_CASE("Lib Utils Noop Executes as Soft End", "[stage1][scheduler]") {
    std::string markdown = R"(
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
entry: test_noop
nodes:
  - id: test_noop
    type: assign
    assign:
      before: "start"
    next: "/__system__/noop"
  - id: continue
    type: assign
    assign:
      after: "continued"
    wait_for: ["/__system__/noop"]
    next: /main/end
  - id: end
    type: end
    termination_mode: hard
# --- END AgenticDSL ---
```
)";

    auto ctx = run_dsl(markdown);
    REQUIRE(ctx["before"] == "start");
    REQUIRE(ctx["after"] == "continued");
}

// Test 6: Cross-graph execution - node in /side branch connects to /main branch
// This test verifies nodes can connect across different graphs
TEST_CASE("Cross-Graph Edge Execution", "[scheduler][cross-graph][bug]") {
    std::string markdown = R"(
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
entry: start
nodes:
  - id: start
    type: start
    next: "/side/work"
  - id: end
    type: end
    termination_mode: hard
# --- END AgenticDSL ---
```

### AgenticDSL `/side/work`
```yaml
# --- BEGIN AgenticDSL ---
type: assign
assign:
  side_done: "yes"
next: "/main/end"
# --- END AgenticDSL ---
```
)";

    auto ctx = run_dsl(markdown);
    REQUIRE(ctx["side_done"] == "yes");
}
