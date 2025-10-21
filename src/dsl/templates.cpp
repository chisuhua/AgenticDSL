#include "agenticdsl/dsl/templates.h"
#include <inja/inja.hpp>
#include <exception>

namespace agenticdsl {

InjaTemplateRenderer::InjaTemplateRenderer() : env_() {
    // Set default delimiters
    env_.set_expression("{{", "}}");
    env_.set_statement("{%", "%}");
    env_.set_comment("{#", "#}");
    env_.set_line_statement("##");

    // Configure for security (v1.1)
    configure_security();
}

void InjaTemplateRenderer::configure_security() {
    // Disable include and extend functionality
    env_.set_include_callback([](const std::filesystem::path&, const std::string& name) {
        throw inja::RenderError("Include functionality is disabled for security.");
    });
    // Note: There's no direct API to disable 'extends', but it's rarely used in simple templates.
    // The main risk is 'include', which is now disabled.
    // Additional security could involve sandboxing or pre-parsing templates to remove disallowed constructs.
}

std::string InjaTemplateRenderer::render(std::string_view template_str, const Context& context) {
    static InjaTemplateRenderer renderer; // Use static instance with security config
    try {
        inja::Template temp = renderer.env_.parse(template_str);
        return renderer.env_.render(temp, context);
    } catch (const std::exception& e) {
        // In production, might want more detailed error logging
        return std::string(template_str);
    }
}

std::string InjaTemplateRenderer::render_with_env(std::string_view template_str, const Context& context) {
    try {
        inja::Template temp = env_.parse(template_str);
        return env_.render(temp, context);
    } catch (const std::exception& e) {
        return std::string(template_str);
    }
}

template<typename Func>
void InjaTemplateRenderer::add_callback(const std::string& name, size_t num_args, Func&& func) {
    env_.add_callback(name, num_args, std::forward<Func>(func));
}

template<typename Func>
void InjaTemplateRenderer::add_void_callback(const std::string& name, size_t num_args, Func&& func) {
    env_.add_void_callback(name, num_args, std::forward<Func>(func));
}

// Instantiate templates
template void InjaTemplateRenderer::add_callback<std::function<nlohmann::json(inja::Arguments&)>>(const std::string&, size_t, std::function<nlohmann::json(inja::Arguments&)>);
template void InjaTemplateRenderer::add_void_callback<std::function<void(inja::Arguments&)>>(const std::string&, size_t, std::function<void(inja::Arguments&)>);

} // namespace agenticdsl
