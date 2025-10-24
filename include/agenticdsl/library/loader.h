// agenticdsl/library/loader.h
#ifndef AGENTICDSL_LIBRARY_LOADER_H
#define AGENTICDSL_LIBRARY_LOADER_H

#include "agenticdsl/library/schema.h"
#include "agenticdsl/core/parser.h"
#include <vector>
#include <string>

namespace agenticdsl {

class StandardLibraryLoader {
public:
    static StandardLibraryLoader& instance();
    const std::vector<LibraryEntry>& get_available_libraries() const;
    void load_from_directory(const std::string& lib_dir);
    void load_builtin_libraries();

private:
    StandardLibraryLoader() = default;
    std::vector<LibraryEntry> libraries_;
    MarkdownParser parser_;
};

} // namespace agenticdsl

#endif // AGENTICDSL_LIBRARY_LOADER_H
