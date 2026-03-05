#include "catch_amalgamated.hpp"
#include "common/tools/registry.h"
#include "common/llm/llm_tool.h"

#include <memory>

using namespace agenticdsl;

// Mock LLM tool for testing
class MockLLMTool : public ILLMTool {
public:
    MockLLMTool(const std::string& tool_name) : tool_name_(tool_name) {}

    LLMResult generate(const std::string& prompt, const LLMParams& params = {}) override {
        LLMResult result;
        result.success = true;
        result.text = "Mock response for: " + prompt;
        result.tokens_generated = 10;
        return result;
    }

    bool is_available() const override { return true; }
    std::string name() const override { return tool_name_; }

private:
    std::string tool_name_;
};

TEST_CASE("ToolRegistry can register LLM tool", "[tool_registry][llm_tool]") {
    ToolRegistry registry;
    
    auto llm_tool = std::make_unique<MockLLMTool>("test_llm");
    LLMParams params;
    params.temperature = 0.5f;
    params.max_tokens = 256;
    
    registry.register_llm_tool("test_llm", std::move(llm_tool), params);
    
    REQUIRE(registry.is_llm_tool("test_llm") == true);
}

TEST_CASE("ToolRegistry can check if tool is not LLM tool", "[tool_registry][llm_tool]") {
    ToolRegistry registry;
    
    REQUIRE(registry.is_llm_tool("web_search") == false);
    REQUIRE(registry.is_llm_tool("nonexistent") == false);
}

TEST_CASE("ToolRegistry can get LLM params", "[tool_registry][llm_tool]") {
    ToolRegistry registry;
    
    auto llm_tool = std::make_unique<MockLLMTool>("test_llm");
    LLMParams params;
    params.temperature = 0.5f;
    params.max_tokens = 256;
    params.model = "llama-2";
    
    registry.register_llm_tool("test_llm", std::move(llm_tool), params);
    
    const auto& retrieved_params = registry.get_llm_params("test_llm");
    REQUIRE(retrieved_params.temperature == 0.5f);
    REQUIRE(retrieved_params.max_tokens == 256);
    REQUIRE(retrieved_params.model == "llama-2");
}

TEST_CASE("ToolRegistry can call LLM tool", "[tool_registry][llm_tool]") {
    ToolRegistry registry;
    
    auto llm_tool = std::make_unique<MockLLMTool>("test_llm");
    registry.register_llm_tool("test_llm", std::move(llm_tool), LLMParams{});
    
    auto result = registry.call_llm_tool("test_llm", "Hello world", LLMParams{});
    
    REQUIRE(result.contains("success") == true);
    REQUIRE(result["success"] == true);
    REQUIRE(result.contains("text") == true);
}

TEST_CASE("ToolRegistry returns error for non-existent LLM tool", "[tool_registry][llm_tool]") {
    ToolRegistry registry;
    
    auto result = registry.call_llm_tool("nonexistent", "prompt", LLMParams{});
    
    REQUIRE(result.contains("error") == true);
}

TEST_CASE("ToolRegistry list_tools includes LLM tools", "[tool_registry][llm_tool]") {
    ToolRegistry registry;
    
    auto llm_tool = std::make_unique<MockLLMTool>("test_llm");
    registry.register_llm_tool("test_llm", std::move(llm_tool), LLMParams{});
    
    auto tools = registry.list_tools();
    
    bool found = false;
    for (const auto& t : tools) {
        if (t == "test_llm") {
            found = true;
            break;
        }
    }
    REQUIRE(found == true);
}

TEST_CASE("ToolRegistry has_tool works for LLM tools", "[tool_registry][llm_tool]") {
    ToolRegistry registry;
    
    auto llm_tool = std::make_unique<MockLLMTool>("test_llm");
    registry.register_llm_tool("test_llm", std::move(llm_tool), LLMParams{});
    
    REQUIRE(registry.has_tool("test_llm") == true);
    REQUIRE(registry.has_tool("nonexistent") == false);
}

TEST_CASE("ToolRegistry get_llm_params throws for non-LLM tool", "[tool_registry][llm_tool]") {
    ToolRegistry registry;
    
    REQUIRE_THROWS_AS(registry.get_llm_params("web_search"), std::runtime_error);
}
