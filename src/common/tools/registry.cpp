#include "common/tools/registry.h"
#include <stdexcept>
#include <limits>

namespace agenticdsl {

ToolRegistry::ToolRegistry() {
    register_default_tools(); // ← 构造时注册默认工具
}

void ToolRegistry::register_default_tools() {
    register_tool("web_search", [](const std::unordered_map<std::string, std::string>& args) -> nlohmann::json {
        auto it = args.find("query");
        std::string query = (it != args.end()) ? it->second : "default query";
        return nlohmann::json{{"results", "[MOCK] Search results for: " + query}};
    });

    register_tool("get_weather", [](const std::unordered_map<std::string, std::string>& args) -> nlohmann::json {
        auto it = args.find("location");
        std::string loc = (it != args.end()) ? it->second : "unknown";
        return nlohmann::json{
            {"location", loc},
            {"condition", "Sunny"},
            {"temperature_c", 22}
        };
    });

    register_tool("calculate", [](const std::unordered_map<std::string, std::string>& args) -> nlohmann::json {
        auto a_it = args.find("a");
        auto b_it = args.find("b");
        auto op_it = args.find("op");

        if (a_it == args.end() || b_it == args.end() || op_it == args.end()) {
            return nlohmann::json{{"error", "Missing arguments: a, b, op"}};
        }

        try {
            double a = std::stod(a_it->second);
            double b = std::stod(b_it->second);
            std::string op = op_it->second;

            if (op == "/" && b == 0.0) {
                return nlohmann::json{{"error", "Division by zero"}};
            }

            double result = 0.0;
            if (op == "+") result = a + b;
            else if (op == "-") result = a - b;
            else if (op == "*") result = a * b;
            else if (op == "/") result = a / b;
            else return nlohmann::json{{"error", "Unsupported operator: " + op}};

            return nlohmann::json{{"result", result}};
        } catch (const std::exception& e) {
            return nlohmann::json{{"error", "Invalid number format"}};
        }
    });
}

bool ToolRegistry::has_tool(const std::string& name) const {
    return tools_.count(name) > 0;
}

nlohmann::json ToolRegistry::call_tool(const std::string& name, const std::unordered_map<std::string, std::string>& args) {
    auto it = tools_.find(name);
    if (it == tools_.end()) {
        return nlohmann::json{{"error", "Tool not found: " + name}};
    }

    try {
        return it->second(args);
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", std::string("Tool execution failed: ") + e.what()}};
    }
}

std::vector<std::string> ToolRegistry::list_tools() const {
    std::vector<std::string> names;
    names.reserve(tools_.size());
    for (const auto& [name, _] : tools_) {
        names.push_back(name);
    }
    return names;
}

} // namespace agenticdsl
