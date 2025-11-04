// modules/library/src/library_loader.cpp
#include "library_loader.h"
//#include "core/types/system_nodes.h" // For create_system_nodes if needed, or define built-ins differently
#include <filesystem>
#include <fstream>
#include <sstream>

namespace agenticdsl {

StandardLibraryLoader& StandardLibraryLoader::instance() {
    static StandardLibraryLoader loader;
    static bool initialized = false;
    if (!initialized) {
        loader.load_builtin_libraries();
        // Optional: loader.load_from_directory("./lib");
        initialized = true;
    }
    return loader;
}

void StandardLibraryLoader::load_builtin_libraries() {
    // Register /lib/utils/noop (defined as system node, but conceptually a library)
    // libraries_.push_back({
    //      "/lib/utils/noop",
    //     std::nullopt,
    //     {},
    //     false // is single node
    // });

    // Example: Register /lib/math/add
    // This is just a declaration, actual execution requires the full graph.
    // The graph for /lib/math/add would be loaded separately or defined in a .md file.
    // For v3.1, we can define a signature.
    libraries_.push_back({
         "/lib/math/add",
         "(a: number, b: number) -> {sum: number}",
         // Parsed schema would be calculated by the parser/loader when the actual graph is loaded
         // For built-ins, we might hardcode the schema or parse the signature string here.
         nlohmann::json::parse(R"({"type": "object", "properties": {"sum": {"type": "number"}}})"), // Example schema
         {},
         true // is subgraph
    });

    // Add other built-in library entries as needed per v3.1 spec
    libraries_.push_back({
         "/lib/reasoning/with_rollback",
         "(try_path: string, fallback_path: string) -> {success: boolean}",
         nlohmann::json::parse(R"({"type": "object", "properties": {"success": {"type": "boolean"}}})"), // Example schema
         {},
         true // is subgraph
    });

    // ... add more ...
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
                        LibraryEntry entry;
                        entry.path = g.path;
                        entry.signature = g.signature;
                        entry.output_schema = g.output_schema; // From parser (v3.1)
                        entry.permissions = g.permissions;
                        entry.is_subgraph = true;
                        libraries_.push_back(std::move(entry));
                    }
                }
            } catch (const std::exception& e) {
                // Log error, but don't interrupt loading
                // std::cerr << "[WARNING] Failed to load library from " << entry.path() << ": " << e.what() << std::endl;
                continue;
            }
        }
    }
}

} // namespace agenticdsl
