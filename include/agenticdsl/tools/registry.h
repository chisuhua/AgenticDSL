#ifndef AGENFLOW_REGISTRY_H
#define AGENFLOW_REGISTRY_H

#include "common/types.h"
#include <unordered_map>
#include <functional>
#include <string_view>
#include <vector>

namespace agenticdsl {

class ToolRegistry {
public:
    static ToolRegistry& instance();

    template<typename Func>
    requires std::invocable<Func, const std::unordered_map<std::string, std::string>&>
    void register_tool(std::string_view name, Func&& func);

    bool has_tool(std::string_view name) const;
    nlohmann::json call_tool(std::string_view name, const std::unordered_map<std::string, std::string>& args);
    std::vector<std::string> list_tools() const;

private:
    ToolRegistry() = default;
    std::unordered_map<std::string, std::function<nlohmann::json(const std::unordered_map<std::string, std::string>&)>> tools_;

    static bool register_default_tools();
    static bool init_default_tools;
};

} // namespace agenticdsl

#endif
