// agenticdsl/library/schema.h
#ifndef AGENTICDSL_LIBRARY_SCHEMA_H
#define AGENTICDSL_LIBRARY_SCHEMA_H

#include <string>
#include <vector>
#include <optional>

namespace agenticdsl {

struct LibraryEntry {
    std::string path;
    std::optional<std::string> signature;
    std::vector<std::string> permissions;
    bool is_subgraph = false;
};

} // namespace agenticdsl

#endif // AGENTICDSL_LIBRARY_SCHEMA_H
