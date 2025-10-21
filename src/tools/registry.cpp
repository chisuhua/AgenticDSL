#include "agenticdsl/tools/registry.h"
#include <limits>

namespace agenticdsl {

ToolRegistry& ToolRegistry::instance() {
    static ToolRegistry registry;
    return registry;
}

bool ToolRegistry::has_tool(std::string_view name) const {
    return tools_.find(std::string(name)) != tools_.end();
}

nlohmann::json ToolRegistry::call_tool(std::string_view name, const std::unordered_map<std::string, std::string>& args) {
    auto it = tools_.find(std::string(name));
    if (it == tools_.end()) {
        throw std::runtime_error("Tool '" + std::string(name) + "' not found");
    }
    return it->second(args);
}

std::vector<std::string> ToolRegistry::list_tools() const {
    std::vector<std::string> names;
    names.reserve(tools_.size());
    for (const auto& [name, _] : tools_) {
        names.push_back(name);
    }
    return names;
}

bool ToolRegistry::register_default_tools() {
    instance().register_tool("web_search", [](const std::unordered_map<std::string, std::string>& args) -> nlohmann::json {
        auto query_it = args.find("query");
        std::string query = query_it != args.end() ? query_it->second : "default query";
        return std::string("[MOCK] Search results for: ") + query;
    });

    instance().register_tool("get_weather", [](const std::unordered_map<std::string, std::string>& args) -> nlohmann::json {
        auto location_it = args.find("location");
        std::string location = location_it != args.end() ? location_it->second : "unknown";
        return std::string("[MOCK] Weather in ") + location + ": Sunny, 22°C";
    });

    instance().register_tool("calculate", [](const std::unordered_map<std::string, std::string>& args) -> nlohmann::json {
        auto a_it = args.find("a");
        auto b_it = args.find("b");
        auto op_it = args.find("op");

        if (a_it == args.end() || b_it == args.end() || op_it == args.end()) {
            return std::string("[ERROR] Missing arguments for calculate tool");
        }

        double a = std::stod(a_it->second);
        double b = std::stod(b_it->second);
        std::string op = op_it->second;

        double result = 0.0;
        if (op == "+") result = a + b;
        else if (op == "-") result = a - b;
        else if (op == "*") result = a * b;
        else if (op == "/") result = (b != 0.0) ? a / b : std::numeric_limits<double>::quiet_NaN();

        return result;
    });

    return true;
}

bool ToolRegistry::init_default_tools = ToolRegistry::register_default_tools();

} // namespace agenticdsl
