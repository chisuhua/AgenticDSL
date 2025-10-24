// tests/test_parser.cpp
#include "catch_amalgamated.hpp"
#include "agenticdsl/core/parser.h"
#include "agenticdsl/common/utils.h"
#include "agenticdsl/core/nodes.h"
#include <string>
#include <iostream>

using namespace agenticdsl;

// Test 1: Extract pathed blocks correctly (with multi-line YAML)
TEST_CASE("Parse Pathed Blocks", "[parser][utils]") {
    std::string markdown = R"(
Some intro text.

### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
nodes:
  - id: start
    type: start
    next: ["/main/ask"]
# --- END AgenticDSL ---
```

More text.

### AgenticDSL `/main/ask`
```yaml
# --- BEGIN AgenticDSL ---
type: llm_call
prompt_template: "Hello {{ user.name }}!"
output_keys: "greeting"
next: "/main/end"
# --- END AgenticDSL ---
```

### AgenticDSL `/resources/db`
```yaml
# --- BEGIN AgenticDSL ---
type: resource
resource_type: postgres
uri: "postgresql://localhost/test"
scope: global
# --- END AgenticDSL ---
```
)";

    auto blocks = extract_pathed_blocks(markdown);
    REQUIRE(blocks.size() == 3);

    REQUIRE(blocks[0].first == "/main");
    REQUIRE(blocks[1].first == "/main/ask");
    REQUIRE(blocks[2].first == "/resources/db");

    // Check YAML content is extracted (not empty)
    REQUIRE_FALSE(blocks[0].second.empty());
    REQUIRE_FALSE(blocks[1].second.empty());
    REQUIRE_FALSE(blocks[2].second.empty());
}

// Test 2: Parse single llm_call node
TEST_CASE("Parse LLMCallNode", "[parser]") {
    std::string yaml = R"(
type: llm_call
prompt_template: "Summarize: {{ input }}"
output_keys: "summary"
next: "/main/end"
metadata:
  description: "Summarization step"
)";
    auto json_doc = nlohmann::json::parse(yaml);
    MarkdownParser parser;
    auto node = parser.create_node_from_json("/main/summarize", json_doc);

    REQUIRE(node != nullptr);
    REQUIRE(node->type == NodeType::LLM_CALL);
    REQUIRE(node->path == "/main/summarize");
    REQUIRE(node->next.size() == 1);
    REQUIRE(node->next[0] == "/main/end");
    REQUIRE(node->metadata["description"] == "Summarization step");

    auto* llm_node = dynamic_cast<LLMCallNode*>(node.get());
    REQUIRE(llm_node != nullptr);
    REQUIRE(llm_node->prompt_template == "Summarize: {{ input }}");
    REQUIRE(llm_node->output_keys == std::vector<std::string>{"summary"});
}

// Test 3: Parse tool_call with arguments and array output_keys
TEST_CASE("Parse ToolCallNode", "[parser]") {
    std::string yaml = R"(
type: tool_call
tool: http_get
arguments:
  url: "https://api.example.com?q={{ query }}"
  headers: "{{ default(headers, '{}') }}"
output_keys: ["status", "body"]
next: ["/main/process"]
)";
    auto json_doc = nlohmann::json::parse(yaml);
    MarkdownParser parser;
    auto node = parser.create_node_from_json("/main/fetch", json_doc);

    REQUIRE(node != nullptr);
    REQUIRE(node->type == NodeType::TOOL_CALL);

    auto* tool_node = dynamic_cast<ToolCallNode*>(node.get());
    REQUIRE(tool_node != nullptr);
    REQUIRE(tool_node->tool_name == "http_get");
    REQUIRE(tool_node->arguments.at("url") == "https://api.example.com?q={{ query }}");
    REQUIRE(tool_node->output_keys == std::vector<std::string>{"status", "body"});
    REQUIRE(tool_node->next.size() == 1);
    REQUIRE(tool_node->next[0] == "/main/process");
}

// Test 4: Parse resource node
TEST_CASE("Parse ResourceNode", "[parser]") {
    std::string yaml = R"(
type: resource
resource_type: file
uri: "/data/cache.json"
scope: global
metadata:
  tags: ["cache", "temp"]
)";
    auto json_doc = nlohmann::json::parse(yaml);
    MarkdownParser parser;
    auto node = parser.create_node_from_json("/resources/cache", json_doc);

    REQUIRE(node != nullptr);
    REQUIRE(node->type == NodeType::RESOURCE);
    REQUIRE(node->metadata["tags"] == nlohmann::json::array({"cache", "temp"}));

    auto* res_node = dynamic_cast<ResourceNode*>(node.get());
    REQUIRE(res_node != nullptr);
    REQUIRE(res_node->resource_type == ResourceType::FILE);
    REQUIRE(res_node->uri == "/data/cache.json");
    REQUIRE(res_node->scope == "global");
}

// Test 5: Parse subgraph (/main)
TEST_CASE("Parse Subgraph", "[parser]") {
    std::string markdown = R"(
### AgenticDSL `/main`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
metadata:
  description: "Main workflow"
nodes:
  - id: start
    type: start
    next: ["/main/ask"]
  - id: ask
    type: llm_call
    prompt_template: "What do you need?"
    output_keys: "user_request"
    next: ["/main/end"]
# --- END AgenticDSL ---
```
)";

    MarkdownParser parser;
    auto graphs = parser.parse_from_string(markdown);
    REQUIRE(graphs.size() == 1);

    auto& graph = graphs[0];
    REQUIRE(graph.path == "/main");
    REQUIRE(graph.metadata["description"] == "Main workflow");
    REQUIRE(graph.nodes.size() == 2);

    REQUIRE(graph.nodes[0]->type == NodeType::START);
    REQUIRE(graph.nodes[0]->path == "/main/start");

    REQUIRE(graph.nodes[1]->type == NodeType::LLM_CALL);
    REQUIRE(graph.nodes[1]->path == "/main/ask");
}

// Test 6: output_keys as string vs array
TEST_CASE("OutputKeys Parsing", "[parser]") {
    MarkdownParser parser;

    // String case
    {
        std::string yaml1 = R"(
type: llm_call
prompt_template: "Test"
output_keys: "result"
)";
        auto node1 = parser.create_node_from_json("/test1", nlohmann::json::parse(yaml1));
        auto* llm1 = dynamic_cast<LLMCallNode*>(node1.get());
        REQUIRE(llm1->output_keys == std::vector<std::string>{"result"});
    }

    // Array case
    {
        std::string yaml2 = R"(
type: tool_call
tool: mock_tool
output_keys: ["a", "b"]
arguments: {}
)";
        auto node2 = parser.create_node_from_json("/test2", nlohmann::json::parse(yaml2));
        auto* tool2 = dynamic_cast<ToolCallNode*>(node2.get());
        REQUIRE(tool2->output_keys == std::vector<std::string>{"a", "b"});
    }
}

// Test 7: Invalid path format
TEST_CASE("Invalid Path Format", "[parser]") {
    std::string markdown = R"(
### AgenticDSL `invalid_path`
```yaml
# --- BEGIN AgenticDSL ---
type: assign
assign:
  x: "1"
# --- END AgenticDSL ---
```
)";

    MarkdownParser parser;
    REQUIRE_THROWS_WITH(
        parser.parse_from_string(markdown),
        Catch::Contains("Invalid node path format")
    );
}

// Test 8: Missing required field (e.g., output_keys)
TEST_CASE("Missing Required Field", "[parser]") {
    std::string yaml = R"(
type: llm_call
prompt_template: "Test"
# output_keys missing!
)";
    MarkdownParser parser;
    REQUIRE_THROWS_WITH(
        parser.create_node_from_json("/bad", nlohmann::json::parse(yaml)),
        Catch::Contains("Missing 'output_keys'")
    );
}

TEST_CASE("Parse signature and permissions from subgraph", "[parser][stage3]") {
    std::string markdown = R"(
### AgenticDSL `/lib/math/add`
```yaml
# --- BEGIN AgenticDSL ---
graph_type: subgraph
signature: "(a: number, b: number) -> sum: number"
permissions: ["file:read"]
nodes:
  - id: add
    type: tool_call
    tool: calculate
    arguments: {a: "1", b: "2", op: "+"}
    output_keys: ["sum"]
    next: ["/end_soft"]
# --- END AgenticDSL ---
```
)";
    agenticdsl::MarkdownParser parser;
    auto graphs = parser.parse_from_string(markdown);
    REQUIRE(graphs.size() == 1);
    auto& g = graphs[0];
    REQUIRE(g.path == "/lib/math/add");
    REQUIRE(g.signature == "(a: number, b: number) -> sum: number");
    REQUIRE(g.permissions == std::vector<std::string>{"file:read"});
    REQUIRE(g.is_standard_library == true);
}

TEST_CASE("Parse signature and permissions from single node", "[parser][stage3]") {
    std::string markdown = R"(
### AgenticDSL `/main/tool`
```yaml
# --- BEGIN AgenticDSL ---
type: tool_call
tool: web_search
signature: "(query: string) -> results"
permissions: ["network"]
arguments: {query: "test"}
output_keys: ["out"]
next: ["/main/end"]
# --- END AgenticDSL ---
```
)";
    agenticdsl::MarkdownParser parser;
    auto graphs = parser.parse_from_string(markdown);
    REQUIRE(graphs.size() == 1);
    auto* node = dynamic_cast<agenticdsl::ToolCallNode*>(graphs[0].nodes[0].get());
    REQUIRE(node->signature == "(query: string) -> results");
    REQUIRE(node->permissions == std::vector<std::string>{"network"});
}
