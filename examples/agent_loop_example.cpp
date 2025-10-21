#include "agenticdsl/core/engine.hpp"
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

    std::string agent_prompt = R"(
You are an AI assistant. Your task is to generate AgenticDSL code to fulfill the user's request.
The current context is: {{ task }}

Here is the history of executed DSL code and results:
{% for item in history %}
- DSL: {{ item.dsl_code }}
- Result: {{ item.result }}
{% endfor %}

Generate the next AgenticDSL code block to execute. Only return the DSL code block, nothing else.
The DSL should follow this format:

```yaml
nodes:
  - id: start
    type: start
    next: <next_node_id>
  - id: <node_id>
    type: <node_type>
    # ... other fields based on node type
  - id: end
    type: end
```

)"
    // 3. Agent 循环
    for (int step = 0; step < 5; ++step) { // 最多执行5步
        std::cout << "\n--- Agent Step " << (step + 1) << " ---\n";

        // a. 让 LLM 生成 DSL 代码
        std::string prompt = agenticdsl::InjaTemplateRenderer::render(agent_prompt, agent_context);
        std::cout << "Prompt sent to LLM:\n" << prompt << "\n\n";

        std::string generated_dsl = llm_adapter.generate(prompt);
        std::cout << "LLM Generated DSL:\n" << generated_dsl << "\n";

        // b. 提取 ```yaml ... ``` 代码块
        std::string yaml_block;
        size_t start = generated_dsl.find("```yaml");
        if (start != std::string::npos) {
            start += 7; // Length of "```yaml"
            size_t end = generated_dsl.find("```", start);
            if (end != std::string::npos) {
                yaml_block = generated_dsl.substr(start, end - start);
            }
        }
        if (yaml_block.empty()) {
            std::cout << "Could not extract YAML block from LLM response. Skipping.\n";
            break;
        }

        // c. 将 DSL 封装成 Markdown 格式
        std::string markdown_dsl = "# Agent Generated Flow\n\n```yaml\n" + yaml_block + "\n```\n";

        // d. 创建并执行引擎
        try {
            auto engine = agenticdsl::AgenticDSLEngine::from_markdown(markdown_dsl, agent_context);
            auto result = engine->run(agent_context);

            // e. 更新上下文和历史记录
            agent_context = result.final_context;
            nlohmann::json history_entry;
            history_entry["dsl_code"] = yaml_block;
            history_entry["result"] = result.success ? result.final_context.dump() : result.message;
            agent_context["history"].push_back(history_entry);

            std::cout << "\nExecution Result:\n";
            if (result.success) {
                std::cout << "Success!\nFinal Context: " << result.final_context.dump(2) << "\n";
            } else {
                std::cout << "Failed: " << result.message << "\n";
            }

            // f. 检查是否完成任务 (这里简化为检查特定输出)
            if (agent_context.contains("final_answer") && agent_context["final_answer"].get<std::string>().find("Beijing") != std::string::npos) {
                std::cout << "\nAgent completed the task!\n";
                break;
            }

        } catch (const std::exception& e) {
            std::cout << "Error executing generated DSL: " << e.what() << "\n";
            nlohmann::json history_entry;
            history_entry["dsl_code"] = yaml_block;
            history_entry["result"] = std::string("[ERROR] ") + e.what();
            agent_context["history"].push_back(history_entry);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 短暂延迟
    }

std::cout << "\n--- Final Agent Context ---\n" << agent_context.dump(2) << std::endl;

return 0;
}
