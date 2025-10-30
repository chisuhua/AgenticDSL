#include "llama_adapter.h"
#include <stdexcept>
#include <vector>
#include <cstdlib>

namespace agenticdsl {

LlamaAdapter* g_current_llm_adapter = nullptr;

LlamaAdapter::LlamaAdapter(const Config& config)
    : config_(config),
      model_(nullptr, llama_model_free),
      ctx_(nullptr, llama_free),
      sampler_(nullptr, llama_sampler_free) {

    // Load model
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = 99; // Use all GPU layers if available

    llama_model* raw_model = llama_model_load_from_file(config_.model_path.c_str(), model_params);
    if (!raw_model) {
        throw std::runtime_error("Failed to load model: " + config_.model_path);
    }
    model_.reset(raw_model);

    // Create context
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = config_.n_ctx;
    ctx_params.n_threads = config_.n_threads;
    ctx_params.n_threads_batch = config_.n_threads;

    llama_context* raw_ctx = llama_init_from_model(model_.get(), ctx_params);
    if (!raw_ctx) {
        throw std::runtime_error("Failed to create context");
    }
    ctx_.reset(raw_ctx);

    // Create sampler chain
    auto smpl_params = llama_sampler_chain_default_params();
    llama_sampler* raw_sampler = llama_sampler_chain_init(smpl_params);
    llama_sampler_chain_add(raw_sampler, llama_sampler_init_min_p(config_.min_p, 1));
    llama_sampler_chain_add(raw_sampler, llama_sampler_init_temp(config_.temperature));
    llama_sampler_chain_add(raw_sampler, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    sampler_.reset(raw_sampler);
}

LlamaAdapter::~LlamaAdapter() = default;

std::vector<llama_token> LlamaAdapter::tokenize(const std::string& text, bool add_bos) {
    const llama_vocab* vocab = llama_model_get_vocab(model_.get());
    int32_t n_tokens = llama_tokenize(vocab, text.data(), static_cast<int32_t>(text.size()),
                                      nullptr, 0, add_bos, true);
    if (n_tokens < 0) return {};

    std::vector<llama_token> tokens(n_tokens);
    if (llama_tokenize(vocab, text.data(), static_cast<int32_t>(text.size()),
                       tokens.data(), n_tokens, add_bos, true) < 0) {
        return {};
    }
    return tokens;
}

std::string LlamaAdapter::detokenize(llama_token token) {
    const llama_vocab* vocab = llama_model_get_vocab(model_.get());
    char buf[256] = {0};
    int n = llama_token_to_piece(vocab, token, buf, sizeof(buf) - 1, 0, true);
    if (n < 0) return "";
    return std::string(buf, n);
}

std::string LlamaAdapter::generate(const std::string& prompt) {
    if (!is_loaded()) {
        throw std::runtime_error("Model not loaded");
    }

    // Check if context is empty (first call)
    bool is_first = llama_memory_seq_pos_max(llama_get_memory(ctx_.get()), 0) == -1;

    auto tokens = tokenize(prompt, is_first);
    if (tokens.empty()) {
        throw std::runtime_error("Tokenization failed");
    }

    // Prepare batch
    llama_batch batch = llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()));

    // Decode prompt
    if (llama_decode(ctx_.get(), batch)) {
        throw std::runtime_error("Prompt evaluation failed");
    }

    std::string response;
    for (int i = 0; i < config_.n_predict; ++i) {
        llama_token new_token = llama_sampler_sample(sampler_.get(), ctx_.get(), -1);

        if (llama_vocab_is_eog(llama_model_get_vocab(model_.get()), new_token)) {
            break;
        }

        std::string piece = detokenize(new_token);
        response += piece;

        // Prepare next token
        batch = llama_batch_get_one(&new_token, 1);
        if (llama_decode(ctx_.get(), batch)) {
            break;
        }
    }

    // Reset sampler state for next call
    llama_sampler_reset(sampler_.get());

    return response;
}

bool LlamaAdapter::is_loaded() const {
    return model_ != nullptr && ctx_ != nullptr && sampler_ != nullptr;
}

} // namespace agenticdsl
