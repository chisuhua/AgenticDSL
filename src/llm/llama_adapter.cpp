#include "agenticdsl/llm/llama_adapter.h"
#include <stdexcept>
#include <thread>
#include <chrono>
#include <iostream>

namespace agenticdsl {

LlamaAdapter::LlamaAdapter(const Config& config) : config_(config) {
    load_model();
}

LlamaAdapter::~LlamaAdapter() {
    if (ctx_) {
        llama_free(ctx_);
    }
    if (model_) {
        llama_free_model(model_);
    }
}

void LlamaAdapter::load_model() {
    // 初始化llama参数
    llama_model_params model_params = llama_model_default_params();
    model_params.n_ctx = config_.n_ctx;

    // 加载模型
    model_ = llama_load_model_from_file(config_.model_path.c_str(), model_params);
    if (!model_) {
        throw std::runtime_error("Failed to load model: " + config_.model_path);
    }

    // 创建上下文
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = config_.n_ctx;
    ctx_params.n_threads = config_.n_threads;
    ctx_params.n_threads_batch = config_.n_threads;

    ctx_ = llama_new_context_with_model(model_, ctx_params);
    if (!ctx_) {
        llama_free_model(model_);
        throw std::runtime_error("Failed to create context");
    }
}

std::string LlamaAdapter::generate(const std::string& prompt) {
    if (!is_loaded()) {
        return "[ERROR] Model not loaded";
    }

    // 简化的生成逻辑
    // 实际实现会更复杂，包括tokenization, generation loop等
    std::vector<llama_token> tokens_list;
    tokens_list.resize(prompt.size() + 256); // 预分配空间

    // Tokenize输入
    int n_prompt_tokens = llama_tokenize(model_, prompt.c_str(), tokens_list.data(),
                                        static_cast<int>(tokens_list.size()), true);

    if (n_prompt_tokens < 0) {
        return "[ERROR] Failed to tokenize prompt";
    }

    // 设置KV cache
    llama_set_rng_seed(ctx_, 1234); // 设置随机种子

    // 推理
    for (int i = 0; i < n_prompt_tokens - 1; i++) {
        llama_decode(ctx_, llama_batch_get_one(&tokens_list[i], 1, i, 0));
    }

    // 生成响应
    std::string response;
    for (int i = 0; i < config_.n_predict; i++) {
        llama_token id = 0;

        // 获取最后一个token的logits
        auto logits = llama_get_logits_ith(ctx_, n_prompt_tokens - 1 + i);
        auto n_vocab = llama_n_vocab(model_);

        // 采样下一个token (简化版)
        id = llama_sample_token_greedy(ctx_, logits);

        if (id == llama_token_eos(model_)) {
            break;
        }

        // 解码token
        char buf[8] = {0};
        llama_token_to_piece(model_, id, buf, sizeof(buf), 0, true);
        response += buf;

        // 解码下一个token
        llama_decode(ctx_, llama_batch_get_one(&id, 1, n_prompt_tokens + i, 0));
    }

    return response;
}

std::vector<std::string> LlamaAdapter::generate_batch(const std::vector<std::string>& prompts) {
    std::vector<std::string> results;
    results.reserve(prompts.size());

    for (const auto& prompt : prompts) {
        results.push_back(generate(prompt));
    }

    return results;
}

bool LlamaAdapter::is_loaded() const {
    return model_ != nullptr && ctx_ != nullptr;
}

std::string LlamaAdapter::get_context() {
    if (!is_loaded()) {
        return "[ERROR] Context not available, model not loaded";
    }
    // This is a simplified representation, llama.cpp doesn't provide a direct way
    // to dump the full context as a string. This is just a placeholder.
    return "[CONTEXT DUMP - IMPLEMENTATION DEPENDENT]";
}

} // namespace agenticdsl
