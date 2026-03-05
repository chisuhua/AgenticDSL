#ifndef AGENTICDSL_LLM_LLM_TOOL_H
#define AGENTICDSL_LLM_LLM_TOOL_H

#include <string>
#include <nlohmann/json.hpp>

namespace agenticdsl {

struct LLMParams {
    float temperature = 0.7f;
    int max_tokens = 512;
    float top_p = 0.95f;
    int n_ctx = 2048;
    int n_threads = 4;
    std::string model;
};

struct LLMResult {
    bool success = false;
    std::string text;
    std::string error;
    int tokens_generated = 0;
};

class ILLMTool {
public:
    virtual ~ILLMTool() = default;
    
    virtual LLMResult generate(const std::string& prompt, const LLMParams& params = {}) = 0;
    virtual bool is_available() const = 0;
    virtual std::string name() const = 0;
};

} // namespace agenticdsl

#endif // AGENTICDSL_LLM_LLM_TOOL_H
