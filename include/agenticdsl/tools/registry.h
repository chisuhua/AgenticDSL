#ifndef AGENTICDSL_TOOLS_REGISTRY_H
#define AGENTICDSL_TOOLS_REGISTRY_H

#include "common/types.h"
#include <unordered_map>
#include <functional>
#include <string>
#include <vector>

namespace agenticdsl {

class ToolRegistry {
public:
    static ToolRegistry& instance();

    template<typename Func>
    void register_tool(std::string name, Func&& func);

    bool has_tool(const std::string& name) const;
    nlohmann::json call_tool(const std::string& name, const std::unordered_map<std::string, std::string>& args);
    std::vector<std::string> list_tools() const;

private:
    ToolRegistry() = default;
    void register_default_tools();

    std::unordered_map<std::string, std::function<nlohmann::json(const std::unordered_map<std::string, std::string>&)>> tools_;
};

} // namespace agenticdsl

#endif
