#ifndef AGENTICDSL_CORE_ENGINE_H
#define AGENTICDSL_CORE_ENGINE_H

#include "agenticdsl/core/executor.h"
#include "agenticdsl/core/parser.h"
#include "agenticdsl/llm/llama_adapter.h"
#include "agenticdsl/tools/registry.h"
#include <memory>
#include <string>

namespace agenticdsl {

class AgenticDSLEngine {
public:
    static std::unique_ptr<AgenticDSLEngine> from_markdown(const std::string& markdown_content);
    static std::unique_ptr<AgenticDSLEngine> from_file(const std::string& file_path);

    ExecutionResult run(const Context& context = Context{});
    void append_graphs(const std::vector<ParsedGraph>& new_graphs);

    template<typename Func>
    void register_tool(std::string_view name, Func&& func) {
        ToolRegistry::instance().register_tool(name, std::forward<Func>(func));
    }

    LlamaAdapter* get_llm_adapter() { return llama_adapter_.get(); }

    AgenticDSLEngine(std::vector<ParsedGraph> initial_graphs);
private:

    std::vector<ParsedGraph> full_graphs_;
    std::unique_ptr<ModernFlowExecutor> executor_;
    std::unique_ptr<LlamaAdapter> llama_adapter_;
};

} // namespace agenticdsl

#endif
