#ifndef AGENTICDSL_DSL_SPEC_H
#define AGENTICDSL_DSL_SPEC_H

#include <nlohmann/json.hpp>
#include <string>

namespace agenticdsl {

/**
 * 注意：不校验 path（路径由解析器提供），不校验 next 引用存在性（图级校验）
 */
class NodeValidator {
public:
    static void validate(const nlohmann::json& node_json);
    static void validate_type(const std::string& type);

private:
    static void validate_start(const nlohmann::json& node);
    static void validate_end(const nlohmann::json& node);
    static void validate_assign(const nlohmann::json& node);
    static void validate_llm_call(const nlohmann::json& node);
    static void validate_tool_call(const nlohmann::json& node);
    static void validate_resource(const nlohmann::json& node);
    static void validate_output_keys(const nlohmann::json& node);
    static void validate_next(const nlohmann::json& node);
};

} // namespace agenticdsl

#endif // AGENTICDSL_DSL_SPEC_H
