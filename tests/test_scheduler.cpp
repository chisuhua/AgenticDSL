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
### AgenticDSL `/main/start`
```yaml
# --- BEGIN AgenticDSL ---
type: assign
assign:
  step1: "done"
next: "/main/step2"
# --- END AgenticDSL ---
```

### AgenticDSL `/main/step2`
```yaml
# --- BEGIN AgenticDSL ---
type: assign
assign:
  step2: "done"
next: "/main/end"
# --- END AgenticDSL ---
```

### AgenticDSL `/main/end`
```yaml
# --- BEGIN AgenticDSL ---
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
### AgenticDSL `/main/start`
```yaml
# --- BEGIN AgenticDSL ---
type: assign
assign:
  trigger: "go"
next: ["/task/a", "/task/b"]
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

### AgenticDSL `/main/join`
```yaml
# --- BEGIN AgenticDSL ---
type: assign
assign:
  final: "{{ result_a }}+{{ result_b }}"
wait_for:
  all_of: ["/task/a", "/task/b"]
next: "/main/end"
# --- END AgenticDSL ---
```

### AgenticDSL `/main/end`
```yaml
# --- BEGIN AgenticDSL ---
type: end
termination_mode: hard
# --- END AgenticDSL ---
```
)";

    auto ctx = run_dsl(markdown);
    REQUIRE(ctx["final"] == "A+B");
}

// Test 3: soft termination allows parent flow to continue
TEST_CASE("Soft Termination Continues Parent Flow", "[stage1][scheduler]") {
    std::string markdown_with_dep = R"(
### AgenticDSL `/main/start`
```yaml
# --- BEGIN AgenticDSL ---
type: assign
assign:
  before_lib: "yes"
next: "/__system__/noop"
# --- END AgenticDSL ---
```

### AgenticDSL `/main/after_lib`
```yaml
# --- BEGIN AgenticDSL ---
type: assign
assign:
  after_lib: "executed"
wait_for: ["/__system__/noop"]
next: "/main/end"
# --- END AgenticDSL ---
```

### AgenticDSL `/main/end`
```yaml
# --- BEGIN AgenticDSL ---
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
### AgenticDSL `/main/start`
```yaml
# --- BEGIN AgenticDSL ---
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
### AgenticDSL `/main/test_noop`
```yaml
# --- BEGIN AgenticDSL ---
type: assign
assign:
  before: "start"
next: "/__system__/noop"
# --- END AgenticDSL ---
```

### AgenticDSL `/main/continue`
```yaml
# --- BEGIN AgenticDSL ---
type: assign
assign:
  after: "continued"
wait_for: ["/__system__/noop"]
next: "/main/end"
# --- END AgenticDSL ---
```

### AgenticDSL `/main/end`
```yaml
# --- BEGIN AgenticDSL ---
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
// This test FAILS due to bug in topo_scheduler.cpp:594 - successors filtered by branch path prefix
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
    next: [/side/work]
  - id: continue
    type: assign
    assign:
      main_continued: "yes"
      combined: "{{ started }}_{{ side_done }}"
    next: [/main/end]
  - id: end
    type: end
    termination_mode: hard
# --- END AgenticDSL ---
```

### AgenticDSL `/main/start`
```yaml
# --- BEGIN AgenticDSL ---
type: assign
assign:
  started: "yes"
next: "/side/work"
# --- END AgenticDSL ---
```

### AgenticDSL `/side/work`
```yaml
# --- BEGIN AgenticDSL ---
type: assign
assign:
  side_done: "yes"
next: "/main/continue"
# --- END AgenticDSL ---
```

### AgenticDSL `/main/continue`
```yaml
# --- BEGIN AgenticDSL ---
type: assign
assign:
  main_continued: "yes"
  combined: "{{ started }}_{{ side_done }}"
next: "/main/end"
# --- END AgenticDSL ---
```

### AgenticDSL `/main/end`
```yaml
# --- BEGIN AgenticDSL ---
type: end
termination_mode: hard
# --- END AgenticDSL ---
```
)";

    auto ctx = run_dsl(markdown);
    REQUIRE(ctx["started"] == "yes");
    REQUIRE(ctx["side_done"] == "yes");
    REQUIRE(ctx["main_continued"] == "yes");
    REQUIRE(ctx["combined"] == "yes_yes");
}
