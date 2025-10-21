#ifndef AGENFLOW_LLAMA_ADAPTER_H
#define AGENFLOW_LLAMA_ADAPTER_H

#include "common/types.h"
#include <string>
#include <memory>
#include <vector>
#include <llama.h> // llama.cpp header

namespace agenticdsl {

class LlamaAdapter {
public:
    struct Config {
        std::string model_path;
        int n_ctx = 2048;
        int n_threads = 4;
        float temperature = 0.7f;
        int n_predict = 512;
        std::string system_prompt = "You are a helpful assistant that generates AgenticDSL code.";
    };

    LlamaAdapter(const Config& config);
    ~LlamaAdapter();

    // 生成文本
    std::string generate(const std::string& prompt);

    // 批量生成（用于并发）
    std::vector<std::string> generate_batch(const std::vector<std::string>& prompts);

    // 检查模型是否加载成功
    bool is_loaded() const;

    // 获取当前上下文（用于调试）
    std::string get_context();

private:
    Config config_;
    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    std::string system_prompt_;

    void load_model();
    std::string tokenize_and_generate(const std::string& prompt);
    std::vector<llama_token> tokenize(const std::string& text);
    std::string detokenize(const std::vector<llama_token>& tokens);
};

} // namespace agenticdsl

#endif
