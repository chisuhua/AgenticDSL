#ifndef AGENTICDSL_LLM_LLAMA_ADAPTER_H
#define AGENTICDSL_LLM_LLAMA_ADAPTER_H

//#include "common/types.h"
#include <string>
#include <memory>
#include <vector>
#include <llama.h>

namespace agenticdsl {



class LlamaAdapter {
public:
    struct Config {
        std::string model_path;
        int n_ctx = 2048;
        int n_threads = 4;
        float temperature = 0.7f;
        float min_p = 0.05f;
        int n_predict = 512;
    };

    explicit LlamaAdapter(const Config& config);
    ~LlamaAdapter();

    std::string generate(const std::string& prompt);
    bool is_loaded() const;

private:
    Config config_;
    std::unique_ptr<llama_model, decltype(&llama_model_free)> model_;
    std::unique_ptr<llama_context, decltype(&llama_free)> ctx_;
    std::unique_ptr<llama_sampler, decltype(&llama_sampler_free)> sampler_;

    std::vector<llama_token> tokenize(const std::string& text, bool add_bos);
    std::string detokenize(llama_token token);
};

extern LlamaAdapter* g_current_llm_adapter; // ← 临时全局指针
} // namespace agenticdsl

#endif
