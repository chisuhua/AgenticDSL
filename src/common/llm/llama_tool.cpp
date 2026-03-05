#include "common/llm/llama_tool.h"
#include <stdexcept>

namespace agenticdsl {

LlamaTool::LlamaTool(const LlamaAdapter::Config& config)
    : config_(config), adapter_(std::make_unique<LlamaAdapter>(config)) {
}

LlamaTool::~LlamaTool() = default;

LlamaAdapter::Config LlamaTool::convert_params(const LLMParams& params) const {
    LlamaAdapter::Config config = config_;
    
    if (params.temperature > 0.0f) {
        config.temperature = params.temperature;
    }
    if (params.max_tokens > 0) {
        config.n_predict = params.max_tokens;
    }
    if (params.top_p > 0.0f && params.top_p < 1.0f) {
        config.min_p = 1.0f - params.top_p;
    }
    if (params.n_ctx > 0) {
        config.n_ctx = params.n_ctx;
    }
    if (params.n_threads > 0) {
        config.n_threads = params.n_threads;
    }
    if (!params.model.empty()) {
        config.model_path = params.model;
    }
    
    return config;
}

LLMResult LlamaTool::generate(const std::string& prompt, const LLMParams& params) {
    LLMResult result;
    
    try {
        LlamaAdapter::Config run_config = convert_params(params);
        
        if (!adapter_ || !adapter_->is_loaded()) {
            result.success = false;
            result.error = "LLM model not loaded";
            return result;
        }
        
        std::string text = adapter_->generate(prompt);
        
        result.success = true;
        result.text = text;
        result.tokens_generated = static_cast<int>(text.length() / 4);
    } catch (const std::exception& e) {
        result.success = false;
        result.error = e.what();
    }
    
    return result;
}

bool LlamaTool::is_available() const {
    return adapter_ && adapter_->is_loaded();
}

std::string LlamaTool::name() const {
    return "llama";
}

} // namespace agenticdsl
