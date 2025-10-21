#include "agenticdsl/core/engine.h"
#include "common/utils.h"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    // 1. 初始化 LLM 适配器 (假设模型路径正确)
    agenticdsl::LlamaAdapter::Config llm_config;
    llm_config.model_path = "models/qwen-0.6b.gguf"; // 替换为实际模型路径
    llm_config.n_ctx = 2048;
    llm_config.n_threads = std::thread::hardware_concurrency();
    agenticdsl::LlamaAdapter llm_adapter(llm_config);

    if (!llm_adapter.is_loaded()) {
        std::cerr << "Failed to load LLM model. Please check the path: " << llm_config.model_path << std::endl;
        return 1;
    }

    std::cout << "LLM Model loaded successfully.\n";

    // 2. 初始化上下文
    agenticdsl::Context agent_context;
    agent_context["task"] = "Calculate 15 + 27 and then get the weather in Beijing.";
    agent_context["history"] = nlohmann::json::array(); // 记录执行历史

    // v1.1: Example prompt for LLM to generate AgenticDSL v1.1 format
    std::string agent_prompt = R"(
You are an AI assistant. Your task is to generate AgenticDSL code to fulfill the user's request.
The current context is: {{ task }}

Here is the history of executed DSL code and results:
{% for item in history %}
- DSL: {{ item.dsl_code }}
- Result: {{ item.result }}
{% endfor %}

Generate the next AgenticDSL code block to execute. Follow the AgenticDSL v1.1 specification:
1. Each node starts with `### AgenticDSL '/path'`
2. Use `/main/step1`, `/main/step2`, etc. for your paths
3. Use `assign` instead of `set`
4. Use `arguments` instead of `args`
5. Use `output_keys` (can be a string or list) instead of `output_key`
6. Use Inja syntax for all dynamic content: `{{ variable }}`, `{% if condition %}...{% endif %}`, etc.
7. If you need to reference external resources, first define a `resource` node like:
   ```markdown
   ### AgenticDSL `/resources/weather_cache`
   ```yaml
   # --- BEGIN AgenticDSL ---
   type: resource
   resource_type: file
   uri: "/tmp/weather_cache.json"
   scope: global
   # --- END AgenticDSL ---
   ```
   ```
   Then reference it in other nodes like `{{ resources.weather_cache.uri }}`.
   Remember: Do NOT try to modify `resources.xxx` from within a node. Only read from it.
   Use a dedicated tool for writing if needed.

The DSL should follow this format:

### AgenticDSL `/main/step1`
```yaml
# --- BEGIN AgenticDSL ---
type: start
next: ["/main/step2"]
# --- END AgenticDSL ---
```

### AgenticDSL `/main/step2`
```yaml
# --- BEGIN AgenticDSL ---
type: assign
assign:
  location: "{{ user_input }}"
  query: "weather in {{ location }}"
output_keys: ["loc", "qry"]
next: ["/main/step3"]
# --- END AgenticDSL ---
```

)";

    // 3. Agent 循环 (Simplified for v1.1)
    for (int step = 0; step < 2; ++step) { // 最多执行2步
        std::cout << "\n--- Agent Step " << (step + 1) << " ---\n";

        // a. 让 LLM 生成 DSL 代码 (v1.1 format)
        std::string prompt = agenticdsl::InjaTemplateRenderer::render(agent_prompt, agent_context);
        std::cout << "Prompt sent to LLM:\n" << prompt << "\n\n";

        std::string generated_dsl = llm_adapter.generate(prompt);
        std::cout << "LLM Generated DSL:\n" << generated_dsl << "\n";

        // b. 提取 AgenticDSL v1.1 blocks (简化处理，实际需要更复杂的解析)
        // Here we assume the LLM generates the correct format as per prompt
        // Use the utility function to extract blocks
        auto blocks = agenticdsl::extract_pathed_blocks(generated_dsl);
        std::string aggregated_dsl_content = "# AgenticDSL v1.1 Generated Flow\n";
        for (const auto& [path, yaml_content] : blocks) {
             aggregated_dsl_content += "### AgenticDSL `" + path + "`\n```yaml\n# --- BEGIN AgenticDSL ---\n" + yaml_content + "\n# --- END AgenticDSL ---\n```\n";
        }

        // c. 创建并执行引擎
        try {
            auto engine = agenticdsl::AgenticDSLEngine::from_markdown(aggregated_dsl_content, agent_context);
            auto result = engine->run(agent_context);

            // e. 更新上下文和历史记录
            agent_context = result.final_context;
            nlohmann::json history_entry;
            history_entry["dsl_code"] = aggregated_dsl_content;
            history_entry["result"] = result.success ? result.final_context.dump() : result.message;
            agent_context["history"].push_back(history_entry);

            std::cout << "\nExecution Result:\n";
            if (result.success) {
                std::cout << "Success!\nFinal Context: " << result.final_context.dump(2) << "\n";
            } else {
                std::cout << "Failed: " << result.message << "\n";
            }

        } catch (const std::exception& e) {
            std::cout << "Error executing generated DSL: " << e.what() << "\n";
            nlohmann::json history_entry;
            history_entry["dsl_code"] = aggregated_dsl_content;
            history_entry["result"] = std::string("[ERROR] ") + e.what();
            agent_context["history"].push_back(history_entry);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 短暂延迟
    }

    std::cout << "\n--- Final Agent Context ---\n" << agent_context.dump(2) << std::endl;

    return 0;
}

