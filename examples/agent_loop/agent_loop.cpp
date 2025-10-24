#include "agenticdsl/core/engine.h"
#include "agenticdsl/llm/prompt_builder.h"
#include <iostream>
#include <fstream>
#include <sstream>

// 加载初始 DSL 文件
std::string load_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// 构建包含历史和可用库的 prompt
std::string build_prompt(const agenticdsl::Context& ctx, const std::string& paused_at) {
    // 使用 Inja 模板构建更结构化的 prompt
    std::string base_prompt = R"(
You are an AI agent that generates AgenticDSL code to continue the workflow.

Current context:
{{ context | dump(2) }}

You are paused at node: {{ paused_at }}

Available standard libraries:
{% for lib in available_subgraphs %}
- Path: {{ lib.path }}
  {% if lib.signature %}Signature: {{ lib.signature }}{% endif %}
  Permissions: {{ lib.permissions | join(", ") or "none" }}
{% endfor %}

Generate ONLY the next AgenticDSL block(s) in the exact format below. Do NOT explain.

### AgenticDSL `/main/stepX`
```yaml
# --- BEGIN AgenticDSL ---
type: ...
...
# --- END AgenticDSL ---
```
)";

    agenticdsl::Context prompt_ctx;
    prompt_ctx["context"] = ctx;
    prompt_ctx["paused_at"] = paused_at;

    return agenticdsl::PromptBuilder::inject_libraries_into_prompt(base_prompt, prompt_ctx);
}

int main() {
    try {
        // 1. 加载初始 DSL（必须包含 /main 子图）
        auto engine = agenticdsl::DSLEngine::from_file("initial.md");

        // 2. 初始化上下文
        agenticdsl::Context ctx;
        ctx["user_input"] = "Calculate 15 + 27 and then get the weather in Beijing.";
        ctx["history"] = nlohmann::json::array();

        int max_steps = 5; // 防止无限循环
        int step = 0;

        while (step < max_steps) {
            std::cout << "\n--- Agent Step " << (step + 1) << " ---\n";

            // 3. 执行当前 DAG
            auto result = engine->run(ctx);
            ctx = result.final_context;

            if (!result.success) {
                std::cerr << "❌ Execution failed: " << result.message << "\n";
                break;
            }

            // 4. 检查是否暂停（LLM_CALL 节点）
            if (result.paused_at) {
                std::cout << "⏸️  Paused at: " << *result.paused_at << "\n";

                // 5. 构建 prompt（含可用标准库）
                std::string prompt = build_prompt(ctx, *result.paused_at);
                std::cout << "📝 Prompt:\n" << prompt << "\n";

                // 6. 调用 LLM 生成新 DSL
                std::string new_dsl = engine->get_llm_adapter()->generate(prompt);
                std::cout << "🤖 LLM Generated DSL:\n" << new_dsl << "\n";

                // 7. 记录历史
                nlohmann::json history_entry;
                history_entry["prompt"] = prompt;
                history_entry["generated_dsl"] = new_dsl;
                ctx["history"].push_back(history_entry);

                // 8. 【关键】使用 continue_with_generated_dsl 解析并合并
                engine->continue_with_generated_dsl(new_dsl);

                std::cout << "✅ Appended new blocks. Continuing...\n";
                step++;
            } else {
                std::cout << "✅ Workflow completed successfully.\n";
                std::cout << "Final context:\n" << ctx.dump(2) << "\n";
                break;
            }
        }

        if (step >= max_steps) {
            std::cout << "⚠️  Max steps reached. Terminating.\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "💥 Fatal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
