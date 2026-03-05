#ifndef COMMON_TOOLS_REGISTRY_H
#define COMMON_TOOLS_REGISTRY_H

#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/llm/llm_tool.h"

namespace agenticdsl {

class ToolRegistry {
public:
    ToolRegistry();

    template<typename Func>
    void register_tool(std::string name, Func&& func) {
        tools_[std::move(name)] = std::forward<Func>(func);
    }

    bool has_tool(const std::string& name) const;
    nlohmann::json call_tool(const std::string& name, const std::unordered_map<std::string, std::string>& args);
    std::vector<std::string> list_tools() const;

    // LLM tool methods
    void register_llm_tool(std::string name, std::unique_ptr<ILLMTool> tool, const LLMParams& default_params = {});
    bool is_llm_tool(const std::string& name) const;
    const LLMParams& get_llm_params(const std::string& name) const;
    nlohmann::json call_llm_tool(const std::string& name, const std::string& prompt, const LLMParams& params = {});

private:
    void register_default_tools();
    std::unordered_map<
        std::string,
        std::function<nlohmann::json(const std::unordered_map<std::string, std::string>&)>
    > tools_;

    // LLM tool storage
    struct LLMToolEntry {
        std::unique_ptr<ILLMTool> tool;
        LLMParams default_params;
    };
    std::unordered_map<std::string, LLMToolEntry> llm_tools_;
};

} // namespace agenticdsl

#endif // COMMON_TOOLS_REGISTRY_H
