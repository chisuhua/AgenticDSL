// agenticdsl/llm/prompt_builder.h
#ifndef AGENTICDSL_LLM_PROMPT_BUILDER_H
#define AGENTICDSL_LLM_PROMPT_BUILDER_H

#include "common/types.h"
#include <string>

namespace agenticdsl {

class PromptBuilder {
public:
    static nlohmann::json build_available_libraries_context();
    static std::string inject_libraries_into_prompt(
        const std::string& base_prompt,
        const Context& context
    );
};

} // namespace agenticdsl

#endif
