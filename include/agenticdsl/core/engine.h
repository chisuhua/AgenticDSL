#ifndef AGENFLOW_ENGINE_H
#define AGENFLOW_ENGINE_H

#include "executor.h"
#include "parser.h"
#include "llm/llama_adapter.h"
#include <memory>
#include <concepts>

namespace agenticdsl {

class AgenticDSLEngine {
public:
    static std::unique_ptr<AgenticDSLEngine> from_markdown(const std::string& markdown_content,
                                                          const Context& initial_context = Context{});
    static std::unique_ptr<AgenticDSLEngine> from_file(const std::string& file_path,
                                                      const Context& initial_context = Context{});

    ExecutionResult run(const Context& context = Context{});

    template<typename Func>
    requires std::invocable<Func, const std::unordered_map<std::string, std::string>&>
    void register_tool(std::string_view name, Func&& func);

    // 获取引擎的 LLM 适配器
    LlamaAdapter* get_llm_adapter() { return llama_adapter_.get(); }

private:
    std::unique_ptr<ModernFlowExecutor> executor_;
    std::unique_ptr<LlamaAdapter> llama_adapter_;

    AgenticDSLEngine(std::vector<std::unique_ptr<Node>> nodes);
};

} // namespace agenticdsl

#endif
