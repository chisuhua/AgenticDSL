// modules/library/include/library/schema.h
#ifndef AGENTICDSL_MODULES_LIBRARY_SCHEMA_H
#define AGENTICDSL_MODULES_LIBRARY_SCHEMA_H

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp> // For storing JSON Schema

namespace agenticdsl {

struct LibraryEntry {
    std::string path;
    std::optional<std::string> signature; // Original signature string
    std::optional<nlohmann::json> output_schema; // Parsed JSON Schema from signature (v3.1)
    std::vector<std::string> permissions;
    bool is_subgraph = false;
};

} // namespace agenticdsl

#endif // AGENTICDSL_MODULES_LIBRARY_SCHEMA_H
