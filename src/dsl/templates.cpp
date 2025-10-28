#include "agenticdsl/dsl/templates.h"
#include <inja/inja.hpp>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace agenticdsl {

InjaTemplateRenderer::InjaTemplateRenderer() : env_() {
    env_.set_expression("{{", "}}");
    env_.set_statement("{%", "%}");
    env_.set_comment("{#", "#}");
    env_.set_line_statement("##");

    configure_security();
}

void InjaTemplateRenderer::configure_security() {
    // Inja v3: set_include_callback expects a function returning inja::Template
    env_.set_include_callback([](const std::filesystem::path&, const std::string&) -> inja::Template {
        throw inja::InjaError("render_error", "Include is disabled for security.", inja::SourceLocation{});
    });
}

std::string InjaTemplateRenderer::render(std::string_view template_str, const Context& context) {
    static InjaTemplateRenderer renderer;
    try {
        return renderer.env_.render(template_str, context);
    } catch (const inja::InjaError& e) {
        throw std::runtime_error("Template render error: " + e.message);
    }
}

std::string InjaTemplateRenderer::render_with_env(std::string_view template_str, const Context& context) {
    try {
        return env_.render(template_str, context);
    } catch (const inja::InjaError& e) {
        throw std::runtime_error("Template render error: " + e.message);
    }
}

} // namespace agenticdsl
