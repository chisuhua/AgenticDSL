// tests/test_library_loader.cpp
#include "catch_amalgamated.hpp"
#include "agenticdsl/library/loader.h"

TEST_CASE("StandardLibraryLoader loads builtin noop", "[library][stage3]") {
    auto& loader = agenticdsl::StandardLibraryLoader::instance();
    const auto& libs = loader.get_available_libraries();
    bool found = false;
    for (const auto& lib : libs) {
        if (lib.path == "/lib/utils/noop") {
            found = true;
            break;
        }
    }
    REQUIRE(found);
}
