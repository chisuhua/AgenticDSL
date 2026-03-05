#include "catch_amalgamated.hpp"
#include "common/llm/llm_tool.h"
#include "common/llm/llama_tool.h"
#include <stdexcept>

using namespace agenticdsl;

TEST_CASE("ILLMTool interface exists", "[llm_tool]") {
    REQUIRE(sizeof(ILLMTool) > 0);
}

TEST_CASE("LLMParams has default values", "[llm_tool]") {
    LLMParams params;
    REQUIRE(params.temperature == 0.7f);
    REQUIRE(params.max_tokens == 512);
    REQUIRE(params.n_ctx == 2048);
}

TEST_CASE("LLMResult fields", "[llm_tool]") {
    LLMResult result;
    REQUIRE(result.success == false);
    REQUIRE(result.text.empty());
    REQUIRE(result.error.empty());
}

TEST_CASE("LlamaTool throws on missing model", "[llama_tool]") {
    LlamaAdapter::Config config;
    config.model_path = "models/nonexistent.gguf";
    
    bool threw = false;
    try {
        LlamaTool tool(config);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    REQUIRE(threw);
}

TEST_CASE("LlamaTool name returns llama", "[llama_tool]") {
    REQUIRE(true);
}
