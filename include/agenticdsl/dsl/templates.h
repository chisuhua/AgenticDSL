#ifndef AGENTICDSL_DSL_TEMPLATES_H
#define AGENTICDSL_DSL_TEMPLATES_H

#include "common/types.h"
#include <inja/inja.hpp>
#include <string>
#include <string_view>

namespace agenticdsl {

class InjaTemplateRenderer {
public:
    InjaTemplateRenderer();

    static std::string render(std::string_view template_str, const Context& context);

    std::string render_with_env(std::string_view template_str, const Context& context);

    // Remove explicit template instantiations — not needed in v3

private:
    inja::Environment env_;
    void configure_security();
};

} // namespace agenticdsl

#endif
