#ifndef AGENTICDSL_LLM_LLAMA_TOOL_H
#define AGENTICDSL_LLM_LLAMA_TOOL_H

#include "common/llm/llm_tool.h"
#include "common/llm/llama_adapter.h"

#include <memory>
#include <string>

namespace agenticdsl {

class LlamaTool : public ILLMTool {
public:
    explicit LlamaTool(const LlamaAdapter::Config& config);
    ~LlamaTool() override;
    
    LLMResult generate(const std::string& prompt, const LLMParams& params = {}) override;
    bool is_available() const override;
    std::string name() const override;
    
private:
    LlamaAdapter::Config config_;
    std::unique_ptr<LlamaAdapter> adapter_;
    
    LlamaAdapter::Config convert_params(const LLMParams& params) const;
};

} // namespace agenticdsl

#endif // AGENTICDSL_LLM_LLAMA_TOOL_H
