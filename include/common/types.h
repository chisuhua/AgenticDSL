#ifndef AGENFLOW_TYPES_H
#define AGENFLOW_TYPES_H

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>
#include <variant>
#include <concepts>
#include <string_view>
#include <nlohmann/json.hpp> // inja 依赖 nlohmann/json
#include <optional>

namespace agenticdsl {

// 使用 nlohmann::json 作为统一的数据类型
using Value = nlohmann::json;
using Context = nlohmann::json;

// 节点ID类型 - 现在对应路径
using NodePath = std::string; // e.g., "/main/step1"

// 节点类型枚举
enum class NodeType : uint8_t {
    START,
    END,
    ASSIGN, // v1.1: renamed from SET
    LLM_CALL,
    TOOL_CALL,
    RESOURCE // v1.1: new type
};

// 资源类型枚举
enum class ResourceType : uint8_t {
    FILE,
    POSTGRES,
    MYSQL,
    SQLITE,
    API_ENDPOINT,
    VECTOR_STORE,
    CUSTOM
};

// 工具函数类型 - 现在使用 nlohmann::json
using ToolFunction = std::function<nlohmann::json(const std::unordered_map<std::string, std::string>&)>;

// 执行结果
struct ExecutionResult {
    bool success;
    std::string message; // 错误信息或成功信息
    Context final_context; // 执行结束时的上下文
    std::optional<NodePath> paused_at; // set if paused at llm_call
};

// 资源定义
struct Resource {
    NodePath path; // e.g., /resources/weather_cache
    ResourceType resource_type;
    std::string uri;
    std::string scope; // "global" or "local"
    nlohmann::json metadata; // optional
};

} // namespace agenticdsl

#endif
