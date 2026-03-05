// tests/test_executor.cpp
#include "catch_amalgamated.hpp"
#include "modules/executor/node_executor.h"
#include "common/tools/registry.h"
#include "common/llm/llm_tool.h"
#include "core/types/context.h"

#include <memory>
#include <string>

using namespace agenticdsl;

// Mock LLM tool for testing DSLNode execution
class MockLLMTool : public ILLMTool {
public:
    MockLLMTool(const std::string& tool_name) : tool_name_(tool_name) {}

    LLMResult generate(const std::string& prompt, const LLMParams& params = {}) override {
        LLMResult result;
        result.success = true;
        result.text = "Mock response for: " + prompt;
        result.tokens_generated = 10;
        last_prompt_ = prompt;
        last_params_ = params;
        return result;
    }

    bool is_available() const override { return true; }
    std::string name() const override { return tool_name_; }

    const std::string& get_last_prompt() const { return last_prompt_; }
    const LLMParams& get_last_params() const { return last_params_; }

private:
    std::string tool_name_;
    std::string last_prompt_;
    LLMParams last_params_;
};

// Test 1: DSLNode execution with mock LLM tool
TEST_CASE("DSLNode execution renders prompt and calls LLM tool", "[executor][dsl_node]") {
    ToolRegistry registry;
    
    // Register mock LLM tool
    auto mock_llm = std::make_unique<MockLLMTool>("test_llm");
    LLMParams params;
    params.temperature = 0.3f;
    params.max_tokens = 128;
    registry.register_llm_tool("test_llm", std::move(mock_llm), params);
    
    // Create executor with registry (no real LLM adapter needed)
    NodeExecutor executor(registry, nullptr);
    
    // Create DSLNode
    DSLNode node(
        "/main/generate",
        "Hello {{ name }}!",  // prompt_template
        "test_llm",           // llm_tool_name
        LLMParams{},          // use defaults
        {"response"},         // output_keys
        {"/main/end"}         // next
    );
    
    // Create context with variables
    Context ctx;
    ctx["name"] = "World";
    
    // Execute
    Context result = executor.execute_node(&node, ctx);
    
    // Verify output in context
    REQUIRE(result.contains("response"));
    REQUIRE(result["response"] == "Mock response for: Hello World!");
}

// Test 2: DSLNode execution uses custom LLM params
TEST_CASE("DSLNode execution passes LLM params", "[executor][dsl_node]") {
    ToolRegistry registry;
    
    auto mock_llm = std::make_unique<MockLLMTool>("custom_llm");
    registry.register_llm_tool("custom_llm", std::move(mock_llm), LLMParams{});
    
    NodeExecutor executor(registry, nullptr);
    
    LLMParams custom_params;
    custom_params.temperature = 0.9f;
    custom_params.max_tokens = 1024;
    custom_params.top_p = 0.8f;
    
    DSLNode node(
        "/main/custom",
        "Test prompt",
        "custom_llm",
        custom_params,
        {"output"},
        {}
    );
    
    Context ctx;
    Context result = executor.execute_node(&node, ctx);
    
    REQUIRE(result.contains("output"));
}

// Test 3: DSLNode fails without registered LLM tool
TEST_CASE("DSLNode execution fails for unregistered tool", "[executor][dsl_node]") {
    ToolRegistry registry;
    NodeExecutor executor(registry, nullptr);
    
    DSLNode node(
        "/main/bad",
        "Test",
        "nonexistent_tool",
        LLMParams{},
        {"out"},
        {}
    );
    
    Context ctx;
    REQUIRE_THROWS_AS(executor.execute_node(&node, ctx), std::runtime_error);
}

// Test 4: DSLNode requires output_keys
TEST_CASE("DSLNode execution fails without output_keys", "[executor][dsl_node]") {
    ToolRegistry registry;
    auto mock_llm = std::make_unique<MockLLMTool>("test_llm");
    registry.register_llm_tool("test_llm", std::move(mock_llm), LLMParams{});
    
    NodeExecutor executor(registry, nullptr);
    
    // Create DSLNode without output_keys
    DSLNode node(
        "/main/no_output",
        "Test prompt",
        "test_llm",
        LLMParams{},
        {},  // Empty output_keys
        {}
    );
    
    Context ctx;
    REQUIRE_THROWS_AS(executor.execute_node(&node, ctx), std::runtime_error);
}

// Test 5: ToolCallNode execution works correctly
TEST_CASE("ToolCallNode execution", "[executor]") {
    ToolRegistry registry;
    
    // Register a mock tool
    registry.register_tool("echo", [](const nlohmann::json& args) {
        return nlohmann::json::object({
            {"echoed", args.value("message", "")}
        });
    });
    
    NodeExecutor executor(registry, nullptr);
    
    ToolCallNode node(
        "/main/echo",
        "echo",
        {{"message", "Hello {{ name }}"}},
        {"result"},
        {}
    );
    
    Context ctx;
    ctx["name"] = "World";
    
    Context result = executor.execute_node(&node, ctx);
    
    REQUIRE(result.contains("result"));
    REQUIRE(result["result"]["echoed"] == "Hello World");
}

// Test 6: AssignNode execution
TEST_CASE("AssignNode execution", "[executor]") {
    ToolRegistry registry;
    NodeExecutor executor(registry, nullptr);
    
    AssignNode node(
        "/main/assign",
        {{"greeting", "Hello {{ name }}"}},
        {}
    );
    
    Context ctx;
    ctx["name"] = "World";
    
    Context result = executor.execute_node(&node, ctx);
    
    REQUIRE(result["greeting"] == "Hello World");
}

// Test 7: StartNode and EndNode execution
TEST_CASE("StartNode and EndNode execution", "[executor]") {
    ToolRegistry registry;
    NodeExecutor executor(registry, nullptr);
    
    StartNode start_node("/main/start", {"/main/next"});
    EndNode end_node("/main/end");
    
    Context ctx;
    ctx["value"] = 42;
    
    Context start_result = executor.execute_node(&start_node, ctx);
    REQUIRE(start_result["value"] == 42);
    
    Context end_result = executor.execute_node(&end_node, ctx);
    REQUIRE(end_result["value"] == 42);
}
