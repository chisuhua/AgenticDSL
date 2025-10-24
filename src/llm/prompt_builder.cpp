// src/llm/prompt_builder.cpp
#include "agenticdsl/llm/prompt_builder.h"
#include "agenticdsl/library/loader.h"
#include "agenticdsl/dsl/templates.h"

namespace agenticdsl {

nlohmann::json PromptBuilder::build_available_libraries_context() {
    auto& loader = StandardLibraryLoader::instance();
    nlohmann::json libs = nlohmann::json::array();
    for (const auto& entry : loader.get_available_libraries()) {
        nlohmann::json lib;
        lib["path"] = entry.path;
        if (entry.signature) {
            lib["signature"] = *entry.signature;
        }
        lib["permissions"] = entry.permissions;
        lib["is_subgraph"] = entry.is_subgraph;
        libs.push_back(std::move(lib));
    }
    return libs;
}

std::string PromptBuilder::inject_libraries_into_prompt(
    const std::string& base_prompt,
    const Context& context) {
    Context ctx = context;
    ctx["available_subgraphs"] = build_available_libraries_context();
    return InjaTemplateRenderer::render(base_prompt, ctx);
}

} // namespace agenticdsl
