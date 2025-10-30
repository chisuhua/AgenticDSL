// include/agenticdsl/tools/registry.h
#ifndef COMMON_TOOLS_REGISTRY_H
#define COMMON_TOOLS_REGISTRY_H
#include <string>
#include <unordered_map>
#include <functional>
#include <vector>
#include <nlohmann/json.hpp>

namespace agenticdsl {

class ToolRegistry {
public:
    // 移除 instance()
    ToolRegistry(); // 构造时注册默认工具（可选）

    template<typename Func>
    void register_tool(std::string name, Func&& func) {
        tools_[std::move(name)] = std::forward<Func>(func);
    }

    bool has_tool(const std::string& name) const;
    nlohmann::json call_tool(const std::string& name, const std::unordered_map<std::string, std::string>& args);
    std::vector<std::string> list_tools() const;

private:
    void register_default_tools(); // 可选：是否保留默认工具
    std::unordered_map<
        std::string,
        std::function<nlohmann::json(const std::unordered_map<std::string, std::string>&)>
    > tools_;
};
} // namespace agenticdsl
#endif //COMMON_TOOLS_REGISTRY_H
