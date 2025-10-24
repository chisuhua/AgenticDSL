// tests/test_prompt_builder.cpp
#include "catch_amalgamated.hpp"
#include "agenticdsl/llm/prompt_builder.h"

TEST_CASE("PromptBuilder injects libraries into context", "[llm][stage3]") {
    agenticdsl::Context ctx;
    ctx["user"] = "test";
    std::string prompt = "Hello {{ user }}. Libraries: {{ available_subgraphs | length }}";
    std::string result = agenticdsl::PromptBuilder::inject_libraries_into_prompt(prompt, ctx);
    REQUIRE(result.find("Hello test") != std::string::npos);
    REQUIRE(result.find("Libraries:") != std::string::npos);
}
