#include "common/tools/registry.h"

#include <stdexcept>
#include <limits>

namespace agenticdsl {

ToolRegistry::ToolRegistry() {
    register_default_tools();
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
    return tools_.count(name) > 0 || llm_tools_.count(name) > 0;
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
    names.reserve(tools_.size() + llm_tools_.size());
    for (const auto& [name, _] : tools_) {
        names.push_back(name);
    }
    for (const auto& [name, _] : llm_tools_) {
        names.push_back(name);
    }
    return names;
}

void ToolRegistry::register_llm_tool(std::string name, std::unique_ptr<ILLMTool> tool, const LLMParams& default_params) {
    llm_tools_[std::move(name)] = LLMToolEntry{std::move(tool), default_params};
}

bool ToolRegistry::is_llm_tool(const std::string& name) const {
    return llm_tools_.count(name) > 0;
}

const LLMParams& ToolRegistry::get_llm_params(const std::string& name) const {
    auto it = llm_tools_.find(name);
    if (it == llm_tools_.end()) {
        throw std::runtime_error("Not an LLM tool: " + name);
    }
    return it->second.default_params;
}

nlohmann::json ToolRegistry::call_llm_tool(const std::string& name, const std::string& prompt, const LLMParams& params) {
    auto it = llm_tools_.find(name);
    if (it == llm_tools_.end()) {
        return nlohmann::json{{"error", "LLM tool not found: " + name}};
    }

    try {
        // Merge default params with provided params
        LLMParams merged_params = it->second.default_params;
        if (params.temperature != 0.7f) merged_params.temperature = params.temperature;
        if (params.max_tokens != 512) merged_params.max_tokens = params.max_tokens;
        if (params.top_p != 0.95f) merged_params.top_p = params.top_p;
        if (params.n_ctx != 2048) merged_params.n_ctx = params.n_ctx;
        if (params.n_threads != 4) merged_params.n_threads = params.n_threads;
        if (!params.model.empty()) merged_params.model = params.model;

        auto result = it->second.tool->generate(prompt, merged_params);

        nlohmann::json json_result;
        json_result["success"] = result.success;
        if (result.success) {
            json_result["text"] = result.text;
            json_result["tokens_generated"] = result.tokens_generated;
        } else {
            json_result["error"] = result.error;
        }
        return json_result;
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", std::string("LLM tool execution failed: ") + e.what()}};
    }
}

} // namespace agenticdsl
