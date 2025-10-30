// modules/library/include/library/library_loader.h
#ifndef AGENTICDSL_MODULES_LIBRARY_LIBRARY_LOADER_H
#define AGENTICDSL_MODULES_LIBRARY_LIBRARY_LOADER_H

#include "library/schema.h" // 引入 LibraryEntry
#include "modules/parser/markdown_parser.h" // 引入 ParsedGraph
#include <vector>
#include <string>

namespace agenticdsl {

class StandardLibraryLoader {
public:
    static StandardLibraryLoader& instance();
    const std::vector<LibraryEntry>& get_available_libraries() const;
    void load_from_directory(const std::string& lib_dir);
    void load_builtin_libraries(); // 加载内置子图定义（路径、Schema）

private:
    StandardLibraryLoader() = default;
    std::vector<LibraryEntry> libraries_;
    MarkdownParser parser_; // 内部使用 parser
};

} // namespace agenticdsl

#endif // AGENTICDSL_MODULES_LIBRARY_LIBRARY_LOADER_H
