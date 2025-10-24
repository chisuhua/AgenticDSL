// tests/test_engine_dynamic_dsl.cpp
#include "catch_amalgamated.hpp"
#include "agenticdsl/core/engine.h"

TEST_CASE("Engine appends and executes generated DSL", "[engine][stage3]") {
    std::string initial = R"(
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/llm"]
  - id: llm
    type: llm_call
    prompt_template: "gen"
    output_keys: ["dsl"]
    next: ["/main/end"]
  - id: end
    type: end
# --- END AgenticDSL ---
```
)";
    auto engine = agenticdsl::DSLEngine::from_markdown(initial);
    agenticdsl::Context ctx;
    auto result = engine->run(ctx);
    REQUIRE(result.paused_at.has_value());

    std::string generated = R"(
### AgenticDSL `/main/new`
```yaml
# --- BEGIN AgenticDSL ---
type: assign
assign:
  dynamic_val: "from_generated"
next: ["/main/end"]
# --- END AgenticDSL ---
```
)";
    engine->continue_with_generated_dsl(generated);

    // Re-run with mock LLM output
    ctx["dsl"] = generated;
    auto result2 = engine->run(ctx);
    REQUIRE(result2.success);
    REQUIRE(result2.final_context["dynamic_val"] == "from_generated");
}
