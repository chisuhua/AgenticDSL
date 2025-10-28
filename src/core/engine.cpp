// src/core/engine.cpp
#include "agenticdsl/core/engine.h"
#include "agenticdsl/llm/llama_adapter.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <thread>
#include <stdexcept>
#include <filesystem>

namespace agenticdsl {

static LlamaAdapter::Config load_llm_config(const std::string& config_path = "llm_config.json") {
    namespace fs = std::filesystem;

    LlamaAdapter::Config config;
    config.model_path = "models/qwen-0.6b.gguf"; // default
    config.n_ctx = 2048;
    config.n_threads = std::thread::hardware_concurrency();
    config.temperature = 0.7f;
    config.min_p = 0.05f;
    config.n_predict = 512;

    std::ifstream file(config_path);
    if (!file.is_open()) {
        // Optional: log warning, but proceed with defaults
        return config;
    }

    try {
        nlohmann::json j;
        file >> j;

        if (j.contains("model_path") && j["model_path"].is_string()) {
            std::string model_rel = j["model_path"].get<std::string>();
            // Resolve relative to config file's directory (or current dir)
            fs::path config_dir = fs::path(config_path).parent_path();
            if (config_dir.empty()) config_dir = ".";
            fs::path abs_model_path = fs::absolute(config_dir / model_rel);
            config.model_path = abs_model_path.string();
        }

        if (j.contains("n_ctx") && j["n_ctx"].is_number_integer()) {
            config.n_ctx = j["n_ctx"].get<int>();
        }
        if (j.contains("n_threads") && j["n_threads"].is_number_integer()) {
            int threads = j["n_threads"].get<int>();
            config.n_threads = (threads > 0) ? threads : std::thread::hardware_concurrency();
        }
        if (j.contains("temperature") && j["temperature"].is_number()) {
            config.temperature = static_cast<float>(j["temperature"].get<double>());
        }
        if (j.contains("min_p") && j["min_p"].is_number()) {
            config.min_p = static_cast<float>(j["min_p"].get<double>());
        }
        if (j.contains("n_predict") && j["n_predict"].is_number_integer()) {
            config.n_predict = j["n_predict"].get<int>();
        }
    } catch (const std::exception& e) {
        // Log or ignore; use defaults
    }

    return config;
}

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

    auto config = load_llm_config();

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
std::cout << "Graphs loaded: " << full_graphs_.size() << std::endl;
for (auto& g : full_graphs_) {
    std::cout << "  Graph: " << g.path << " with " << g.nodes.size() << " nodes" << std::endl;
    for (auto& n : g.nodes) {
        std::cout << "    - " << n->path << " (type: " << static_cast<int>(n->type) << ")" << std::endl;
    }
}
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
