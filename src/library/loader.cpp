// src/library/loader.cpp
#include "agenticdsl/library/loader.h"
#include "agenticdsl/core/system_nodes.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace agenticdsl {

StandardLibraryLoader& StandardLibraryLoader::instance() {
    static StandardLibraryLoader loader;
    static bool initialized = false;
    if (!initialized) {
        loader.load_builtin_libraries();
        // 可选：loader.load_from_directory("./lib");
        initialized = true;
    }
    return loader;
}

void StandardLibraryLoader::load_builtin_libraries() {
    // 注册 /lib/utils/noop（已在 system_nodes 中定义）
    libraries_.push_back({
        "/lib/utils/noop",
        std::nullopt,
        {},
        false // 是单个节点
    });

    // 示例：注册 /lib/math/add（需在 DSL 中定义）
    // 此处仅为声明，实际执行仍依赖完整图
    libraries_.push_back({
        "/lib/math/add",
        "(a: number, b: number) -> sum: number",
        {},
        true // 是子图
    });
}

const std::vector<LibraryEntry>& StandardLibraryLoader::get_available_libraries() const {
    return libraries_;
}

void StandardLibraryLoader::load_from_directory(const std::string& lib_dir) {
    namespace fs = std::filesystem;
    if (!fs::exists(lib_dir) || !fs::is_directory(lib_dir)) return;

    for (const auto& entry : fs::recursive_directory_iterator(lib_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".md") {
            std::ifstream file(entry.path());
            std::stringstream buffer;
            buffer << file.rdbuf();
            try {
                auto graphs = parser_.parse_from_string(buffer.str());
                for (const auto& g : graphs) {
                    if (g.is_standard_library) {
                        libraries_.push_back({
                            g.path,
                            g.signature,
                            g.permissions,
                            true
                        });
                    }
                }
            } catch (const std::exception& e) {
                // 可记录日志，但不中断加载
                continue;
            }
        }
    }
}

} // namespace agenticdsl
