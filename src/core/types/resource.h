#ifndef AGENTICDSL_TYPES_RESOURCE_H
#define AGENTICDSL_TYPES_RESOURCE_H

#include "context.h" // 引入 Context/Value
#include "node.h" // 引入 Context/Value
#include <string>
#include <nlohmann/json.hpp>

namespace agenticdsl {
using NodePath = std::string; // e.g., "/main/step1"

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

// 资源定义
struct Resource {
    NodePath path; // e.g., /resources/weather_cache
    ResourceType resource_type;
    std::string uri;
    std::string scope; // "global" or "local"
    nlohmann::json metadata; // optional
};

} // namespace agenticdsl::types

#endif // AGENTICDSL_TYPES_RESOURCE_H
