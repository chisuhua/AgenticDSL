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

    // Inja v3: add_callback(name, func) — NO num_args!
    env_.add_callback("default", [](inja::Arguments& args) -> nlohmann::json {
        return args[0]->is_null() ? *args[1] : *args[0];
    });

    env_.add_callback("exists", [](inja::Arguments& args) -> nlohmann::json {
        return !args[0]->is_null();
    });

    env_.add_callback("length", [](inja::Arguments& args) -> nlohmann::json {
        if (args[0]->is_string()) {
            return static_cast<int>(args[0]->get<std::string>().size());
        } else if (args[0]->is_array()) {
            return static_cast<int>(args[0]->size());
        }
        return 0;
    });

    env_.add_callback("join", [](inja::Arguments& args) -> nlohmann::json {
        std::string sep = args[1]->get<std::string>();
        std::string result;
        const auto& arr = *args[0];
        for (size_t i = 0; i < arr.size(); ++i) {
            if (i > 0) result += sep;
            result += arr[i].get<std::string>();
        }
        return result;
    });

    env_.add_callback("upper", [](inja::Arguments& args) -> nlohmann::json {
        std::string s = args[0]->get<std::string>();
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        return s;
    });

    env_.add_callback("lower", [](inja::Arguments& args) -> nlohmann::json {
        std::string s = args[0]->get<std::string>();
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    });
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
