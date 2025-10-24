// src/core/engine.cpp
#include "agenticdsl/core/engine.h"
#include "agenticdsl/llm/llama_adapter.h"
#include <fstream>
#include <sstream>
#include <thread>
#include <stdexcept>

namespace agenticdsl {

std::unique_ptr<DSLEngine> DSLEngine::from_markdown(const std::string& markdown_content) {
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

    // Initialize LLM adapter
    LlamaAdapter::Config config;
    config.model_path = "models/qwen-0.6b.gguf";
    config.n_ctx = 2048;
    config.n_threads = std::thread::hardware_concurrency();
    auto llama_adapter = std::make_unique<LlamaAdapter>(config);

    auto engine = std::make_unique<DSLEngine>(std::move(graphs));
    engine->llama_adapter_ = std::move(llama_adapter);
    return engine;
}

std::unique_ptr<DSLEngine> DSLEngine::from_file(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + file_path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return from_markdown(buffer.str());
}

DSLEngine::DSLEngine(std::vector<ParsedGraph> initial_graphs)
    : full_graphs_(std::move(initial_graphs)) {
    // 关键修改：使用 DAGExecutor 替代 DAGFlowExecutor
    executor_ = std::make_unique<DAGExecutor>(full_graphs_);
}

ExecutionResult DSLEngine::run(const Context& context) {
    Context exec_context = context;

    //exec_context["__llm__"] = nlohmann::json::object();
    //exec_context["__llm__"]["generate"] = [&](const std::string& prompt) -> std::string {
    //    return llama_adapter_->generate(prompt);
    //};

    LlamaAdapter* prev = g_current_llm_adapter;
    g_current_llm_adapter = llama_adapter_.get();

    auto result = executor_->execute(exec_context);


    g_current_llm_adapter = prev; // 恢复

    return {result.success, result.message, result.final_context, result.paused_at};
}

void DSLEngine::append_graphs(std::vector<ParsedGraph> new_graphs) {
    for (auto& graph : new_graphs) {
        full_graphs_.push_back(std::move(graph));
    }
    executor_ = std::make_unique<DAGExecutor>(full_graphs_);
}

void DSLEngine::continue_with_generated_dsl(const std::string& generated_dsl) {
    if (generated_dsl.empty()) return;

    MarkdownParser parser;
    auto new_graphs = parser.parse_from_string(generated_dsl);

    // 校验：每个新图必须是合法子图（可选：验证 signature / permissions）
    // 此处暂略，后续可集成 NodeValidator
    append_graphs(std::move(new_graphs)); // 复用逻辑
}

} // namespace agenticdsl
