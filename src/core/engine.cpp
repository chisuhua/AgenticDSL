#include "agenticdsl/core/engine.h"
#include "agenticdsl/llm/llama_adapter.h"
#include <fstream>
#include <sstream>
#include <thread>
#include <stdexcept>

namespace agenticdsl {

std::unique_ptr<AgenticDSLEngine> AgenticDSLEngine::from_markdown(const std::string& markdown_content) {
    MarkdownParser parser;
    auto graphs = parser.parse_from_string(markdown_content);

    // Ensure /main exists
    bool has_main = false;
    for (const auto& g : graphs) {
        if (g.path == "/main") {
            has_main = true;
            break;
        }
    }
    if (!has_main) {
        throw std::runtime_error("Required /main subgraph not found");
    }

    LlamaAdapter::Config config;
    config.model_path = "models/qwen-0.6b.gguf";
    config.n_ctx = 2048;
    config.n_threads = std::thread::hardware_concurrency();
    auto llama_adapter = std::make_unique<LlamaAdapter>(config);

    auto engine = std::make_unique<AgenticDSLEngine>(std::move(graphs));
    engine->llama_adapter_ = std::move(llama_adapter);
    return engine;
}

std::unique_ptr<AgenticDSLEngine> AgenticDSLEngine::from_file(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + file_path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return from_markdown(buffer.str());
}

AgenticDSLEngine::AgenticDSLEngine(std::vector<ParsedGraph> initial_graphs)
    : full_graphs_(std::move(initial_graphs)) {
    executor_ = std::make_unique<DAGFlowExecutor>(full_graphs_);
}

AgenticDSLEngine::ExecutionResult AgenticDSLEngine::run(const Context& context) {
    auto result = executor_->execute(context);
    return {result.success, result.message, result.final_context, result.paused_at};
}

void AgenticDSLEngine::append_graphs(const std::vector<ParsedGraph>& new_graphs) {
    full_graphs_.insert(full_graphs_.end(), new_graphs.begin(), new_graphs.end());
    executor_ = std::make_unique<DAGFlowExecutor>(full_graphs_);
}
} // namespace agenticdsl
