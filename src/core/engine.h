// agenticdsl/core/engine.h
#ifndef AGENTICDSL_CORE_ENGINE_H
#define AGENTICDSL_CORE_ENGINE_H

#include "modules/scheduler/topo_scheduler.h" // ← 直接依赖 TopoScheduler
#include "modules/parser/markdown_parser.h"
#include "common/llm/llama_adapter.h"
#include "common/tools/registry.h"
#include <memory>
#include <string>

namespace agenticdsl {

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

    std::vector<TraceRecord> get_last_traces() const { return last_traces_; }

    LlamaAdapter* get_llm_adapter() { return llama_adapter_.get(); }

    DSLEngine(std::vector<ParsedGraph> initial_graphs);
private:

    std::vector<ParsedGraph> full_graphs_;
    ToolRegistry tool_registry_;          // ← 成员变量（非单例）
    std::unique_ptr<LlamaAdapter> llama_adapter_;
    std::vector<TraceRecord> last_traces_; // ← 存储 Trace
};

} // namespace agenticdsl

#endif
