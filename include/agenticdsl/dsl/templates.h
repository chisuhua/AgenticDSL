#ifndef AGENFLOW_TEMPLATES_H
#define AGENFLOW_TEMPLATES_H

#include "common/types.h"
#include <inja/inja.hpp>
#include <string>
#include <string_view>

namespace agenticdsl {

class InjaTemplateRenderer {
public:
    InjaTemplateRenderer();

    // Basic rendering with safety checks
    static std::string render(std::string_view template_str, const Context& context);

    // Rendering with instance environment (for callbacks)
    std::string render_with_env(std::string_view template_str, const Context& context);

    // Register custom callbacks (for v2+ or advanced features)
    template<typename Func>
    void add_callback(const std::string& name, size_t num_args, Func&& func);

    template<typename Func>
    void add_void_callback(const std::string& name, size_t num_args, Func&& func);

    // Get environment reference for advanced configuration
    inja::Environment& get_environment() { return env_; }

private:
    inja::Environment env_;

    // Configure environment for security (disable includes, etc.)
    void configure_security();
};

} // namespace agenticdsl

#endif
