#include "agenticdsl/core/engine.h"
#include <iostream>
#include <fstream>
#include <sstream>

std::string load_file(const std::string& path) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string build_prompt(const agenticdsl::Context& ctx, const std::string& paused_at) {
    // Simplified prompt
    return "Current context: " + ctx.dump() + 
           "\nYou are paused at node: " + paused_at +
           "\nGenerate the next AgenticDSL block(s) to continue.";
}

int main() {
    // 1. Load initial DSL (must contain /main subgraph)
    auto engine = agenticdsl::AgenticDSLEngine::from_file("initial.md");
    
    agenticdsl::Context ctx;
    ctx["user_input"] = "Calculate 15 + 27";

    while (true) {
        auto result = engine->run(ctx);
        ctx = result.context;

        if (!result.success) {
            std::cerr << "Execution failed: " << result.message << "\n";
            break;
        }

        if (result.paused_at) {
            // 2. Generate new DSL
            std::string prompt = build_prompt(ctx, *result.paused_at);
            std::string new_dsl = engine->get_llm_adapter()->generate(prompt);
            
            // 3. Parse and append
            agenticdsl::MarkdownParser parser;
            auto new_graphs = parser.parse_from_string(new_dsl);
            engine->append_graphs(new_graphs);
            
            std::cout << "Appended new blocks. Continuing...\n";
        } else {
            std::cout << "✅ Workflow completed.\n";
            std::cout << "Final context:\n" << ctx.dump(2) << "\n";
            break;
        }
    }

    return 0;
}
