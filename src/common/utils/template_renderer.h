#ifndef AGENTICDSL_COMMON_UTILS_TEMPLATE_RENDERER_H
#define AGENTICDSL_COMMON_UTILS_TEMPLATE_RENDERER_H

#include "core/types/context.h" // 引入 Context (nlohmann::json)
#include <inja/inja.hpp>
#include <string>
#include <string_view>
#include <filesystem> // Required by Inja for set_include_callback

namespace agenticdsl {

class InjaTemplateRenderer {
public:
    InjaTemplateRenderer();

    // 静态方法：使用默认环境渲染模板
    static std::string render(std::string_view template_str, const Context& context);

    // 实例方法：使用当前实例的环境渲染模板
    std::string render_with_env(std::string_view template_str, const Context& context);

private:
    inja::Environment env_;
    void configure_security(); // 配置 Inja 环境以禁用不安全操作
};

} // namespace agenticdsl

#endif // AGENTICDSL_COMMON_UTILS_TEMPLATE_RENDERER_H
