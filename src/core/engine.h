// agenticdsl/core/engine.h
#ifndef AGENTICDSL_CORE_ENGINE_H
#define AGENTICDSL_CORE_ENGINE_H

#include "modules/scheduler/topo_scheduler.h" // ← 直接依赖 TopoScheduler
#include "modules/parser/markdown_parser.h"
#include "llm/llama_adapter.h"
#include "tools/registry.h"
#include <memory>
#include <string>

namespace agenticdsl {

/**
 * DSLEngine: 主引擎类，负责解析、执行、扩展工作流
 */
class DSLEngine {
public:
    static std::unique_ptr<DSLEngine> from_markdown(const std::string& markdown_content);
    static std::unique_ptr<DSLEngine> from_file(const std::string& file_path);

    ExecutionResult run(const Context& context = Context{});
    void continue_with_generated_dsl(const std::string& generated_dsl);

    void append_graphs(std::vector<ParsedGraph> new_graphs);

    template <typename Func>
    void register_tool(std::string_view name, Func&& func) {
        tool_registry_.register_tool(std::string(name), std::forward<Func>(func));
    }

    LlamaAdapter* get_llm_adapter() { return llama_adapter_.get(); }

    DSLEngine(std::vector<ParsedGraph> initial_graphs);
private:

    DSLEngine(std::vector<ParsedGraph> initial_graphs);
    std::vector<ParsedGraph> full_graphs_;
    ToolRegistry tool_registry_;          // ← 成员变量（非单例）
    std::unique_ptr<LlamaAdapter> llama_adapter_;
};

} // namespace agenticdsl

#endif
