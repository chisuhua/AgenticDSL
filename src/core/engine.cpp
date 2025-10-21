#include "agenticdsl/core/engine.h"
#include <fstream>
#include <sstream>

namespace agenticdsl {

std::unique_ptr<AgenticDSLEngine> AgenticDSLEngine::from_markdown(const std::string& markdown_content,
                                                                const Context& initial_context) {
    MarkdownParser parser;
    auto graphs = parser.parse_from_string(markdown_content);

    if (graphs.empty()) {
        throw std::runtime_error("No valid graphs found in markdown content");
    }

    // v1.1: For simplicity, combine all parsed nodes into one executor.
    // In a more complex system, you might handle subgraphs differently.
    std::vector<std::unique_ptr<Node>> all_nodes;
    for (auto& graph : graphs) {
        all_nodes.insert(all_nodes.end(), std::make_move_iterator(graph.nodes.begin()), std::make_move_iterator(graph.nodes.end()));
    }

    auto engine = std::make_unique<AgenticDSLEngine>(std::move(all_nodes));

    // 初始化LLM适配器
    LlamaAdapter::Config config;
    config.model_path = "models/qwen-0.6b.gguf"; // 替换为实际模型路径
    config.n_ctx = 2048;
    config.n_threads = std::thread::hardware_concurrency();
    engine->llama_adapter_ = std::make_unique<LlamaAdapter>(config);

    return engine;
}

std::unique_ptr<AgenticDSLEngine> AgenticDSLEngine::from_file(const std::string& file_path,
                                                             const Context& initial_context) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + file_path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    return from_markdown(content, initial_context);
}

ExecutionResult AgenticDSLEngine::run(const Context& context) {
    if (!executor_) {
        throw std::runtime_error("Executor not initialized");
    }
    return executor_->execute(context);
}

AgenticDSLEngine::AgenticDSLEngine(std::vector<std::unique_ptr<Node>> nodes)
    : executor_(std::make_unique<ModernFlowExecutor>(std::move(nodes))) {}

} // namespace agenticdsl
