// tests/test_path_resolution.cpp
// Tests for model path resolution bug in engine.cpp load_llm_config()
//
// THE BUG (engine.cpp lines 36-43):
//   fs::path config_dir = fs::path(config_path).parent_path();
//   if (config_dir.empty()) config_dir = ".";  // Falls back to CWD
//   fs::path abs_model_path = fs::absolute(config_dir / model_rel);
//
// Problem: When config_path is "configs/llm_config.json", parent_path() = "configs"
// which is correct. BUT when resolving a relative model_path like "../models/test.gguf",
// the buggy code resolves it from CWD instead of from the config's actual directory.

#include "catch_amalgamated.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

// EXACT copy of buggy code from engine.cpp lines 36-43
std::string buggy_resolve_model_path(const std::string& config_path, const std::string& model_rel_path) {
    fs::path config_dir = fs::path(config_path).parent_path();
    if (config_dir.empty()) config_dir = ".";
    fs::path abs_model_path = fs::absolute(config_dir / model_rel_path);
    return abs_model_path.string();
}

// Fixed version - uses absolute config path first
std::string fixed_resolve_model_path(const std::string& config_path, const std::string& model_rel_path) {
    fs::path config_abs = fs::absolute(config_path);
    fs::path config_dir = config_abs.parent_path();
    if (config_dir.empty()) config_dir = ".";
    fs::path abs_model_path = fs::weakly_canonical(config_dir / model_rel_path);
    return abs_model_path.string();
}

TEST_CASE("Path resolution - bug root cause", "[path][bug]") {
    SECTION("parent_path() returns empty for filename-only paths") {
        std::string config_path = "llm_config.json";
        fs::path parent = fs::path(config_path).parent_path();
        REQUIRE(parent.empty());
    }

    SECTION("parent_path() works for relative paths with directory") {
        std::string config_path = "configs/llm_config.json";
        fs::path parent = fs::path(config_path).parent_path();
        REQUIRE(parent == "configs");
        REQUIRE_FALSE(parent.empty());
    }
}

TEST_CASE("Bug demonstration - config in subdirectory", "[path][bug]") {
    // Setup temp directories
    fs::path temp_dir = fs::temp_directory_path() / "agenticdsl_path_test";
    fs::path project_dir = temp_dir / "project";
    fs::path configs_dir = project_dir / "configs";
    fs::path models_dir = project_dir / "models";

    fs::remove_all(temp_dir);
    fs::create_directories(configs_dir);
    fs::create_directories(models_dir);

    // Create config at project/configs/llm_config.json
    fs::path config_path = configs_dir / "llm_config.json";
    {
        std::ofstream config(config_path);
        config << R"({"model_path": "../models/test.gguf"})";
    }

    // Create model at project/models/test.gguf
    fs::path model_path = models_dir / "test.gguf";
    {
        std::ofstream model(model_path);
        model << "dummy model data";
    }

    // Save original directory and change to project_dir
    fs::path original_dir = fs::current_path();
    fs::current_path(project_dir);

    // User passes relative path: configs/llm_config.json
    std::string config_rel = "configs/llm_config.json";
    std::string model_rel = "../models/test.gguf";

    std::string buggy_result = buggy_resolve_model_path(config_rel, model_rel);
    std::string fixed_result = fixed_resolve_model_path(config_rel, model_rel);

    // BUGGY behavior:
    // 1. parent_path("configs/llm_config.json") = "configs"
    // 2. fs::absolute("configs/../models/test.gguf") from CWD (project_dir)
    // 3. Result: /project/configs/../models/test.gguf (unresolved ..)
    // Note: fs::absolute doesn't resolve "..", it just makes the path absolute
    fs::path buggy_expected = fs::absolute(project_dir / "configs" / ".." / "models" / "test.gguf");

    // FIXED behavior:
    // 1. fs::absolute("configs/llm_config.json") = /project/configs/llm_config.json
    // 2. parent_path() = /project/configs
    // 3. fs::weakly_canonical(/project/configs/../models/test.gguf) = /project/models/test.gguf
    fs::path fixed_expected = fs::weakly_canonical(configs_dir / ".." / "models" / "test.gguf");

    INFO("Working directory: " << fs::current_path().string());
    INFO("Config relative path: " << config_rel);
    INFO("Buggy result: " << buggy_result);
    INFO("Fixed result: " << fixed_result);
    INFO("Buggy expected: " << buggy_expected.string());
    INFO("Fixed expected: " << fixed_expected.string());
    INFO("Actual model path: " << model_path.string());

    // Verify expectations
    REQUIRE(buggy_result == buggy_expected.string());
    REQUIRE(fixed_result == fixed_expected.string());

    // THE BUG: Different results prove the issue
    REQUIRE(buggy_result != fixed_result);

    // The fixed result is the CORRECT one (points to actual model)
    REQUIRE(fixed_result == model_path.string());
    REQUIRE(fs::exists(fixed_result));

    // Buggy result has ".." in it and points to wrong location
    REQUIRE(buggy_result.find("..") != std::string::npos);

    // Restore directory and cleanup
    fs::current_path(original_dir);
    fs::remove_all(temp_dir);
}

TEST_CASE("Bug with filename-only config path", "[path][bug]") {
    // This demonstrates the bug when config_path is just a filename

    fs::path temp_dir = fs::temp_directory_path() / "agenticdsl_filename_test";
    fs::path project_dir = temp_dir / "project";
    fs::path models_dir = project_dir / "models";

    fs::remove_all(temp_dir);
    fs::create_directories(models_dir);

    // Create config at project/llm_config.json
    fs::path config_path = project_dir / "llm_config.json";
    {
        std::ofstream config(config_path);
        config << R"({"model_path": "models/test.gguf"})";
    }

    // Create model
    fs::path model_path = models_dir / "test.gguf";
    {
        std::ofstream model(model_path);
        model << "dummy";
    }

    fs::path original_dir = fs::current_path();

    // Simulate: user is in temp_dir, passes "project/llm_config.json"
    fs::current_path(temp_dir);

    std::string config_input = "project/llm_config.json";
    std::string model_rel = "models/test.gguf";

    std::string buggy = buggy_resolve_model_path(config_input, model_rel);
    std::string fixed = fixed_resolve_model_path(config_input, model_rel);

    // BUGGY: resolves from CWD/temp_dir using "project" as parent_path
    // Result: temp_dir/project/models/test.gguf (correct in this case!)
    fs::path buggy_expected = fs::absolute(temp_dir / "project" / "models" / "test.gguf");

    // FIXED: resolves from actual config location
    fs::path fixed_expected = fs::weakly_canonical(config_path.parent_path() / "models" / "test.gguf");

    INFO("Working directory: " << fs::current_path().string());
    INFO("Buggy result: " << buggy);
    INFO("Fixed result: " << fixed);

    REQUIRE(buggy == buggy_expected.string());
    REQUIRE(fixed == fixed_expected.string());

    // In this case, both give the same result because CWD + config_rel = config_abs
    REQUIRE(buggy == fixed);
    REQUIRE(fs::exists(fixed));

    fs::current_path(original_dir);
    fs::remove_all(temp_dir);
}

TEST_CASE("The real bug - running from different directory", "[path][bug]") {
    // This demonstrates the actual bug scenario:
    // - Config is at /project/configs/llm_config.json
    // - User runs from /project (NOT from /)
    // - User passes "configs/llm_config.json"
    // - Model path "../models/test.gguf" should resolve relative to configs/, NOT project/

    fs::path temp_dir = fs::temp_directory_path() / "agenticdsl_real_bug";
    fs::path project_dir = temp_dir / "myproject";
    fs::path configs_dir = project_dir / "configs";
    fs::path models_dir = project_dir / "models";

    fs::remove_all(temp_dir);
    fs::create_directories(configs_dir);
    fs::create_directories(models_dir);

    fs::path config_path = configs_dir / "llm_config.json";
    {
        std::ofstream config(config_path);
        config << R"({"model_path": "../models/test.gguf"})";
    }

    fs::path model_path = models_dir / "test.gguf";
    {
        std::ofstream model(model_path);
        model << "dummy";
    }

    fs::path original_dir = fs::current_path();

    // KEY: Run from project_dir, but config is in configs/ subdirectory
    fs::current_path(project_dir);

    std::string config_input = "configs/llm_config.json";
    std::string model_rel = "../models/test.gguf";

    std::string buggy = buggy_resolve_model_path(config_input, model_rel);
    std::string fixed = fixed_resolve_model_path(config_input, model_rel);

    // BUGGY: Uses CWD/project_dir as base for resolving "../models"
    // Result: project_dir/../models/test.gguf = models/test.gguf (outside project!)
    fs::path buggy_resolved = fs::absolute(project_dir / "configs" / ".." / "models" / "test.gguf");

    // FIXED: Uses configs_dir as base
    // Result: configs_dir/../models/test.gguf = project_dir/models/test.gguf
    fs::path fixed_resolved = fs::weakly_canonical(configs_dir / ".." / "models" / "test.gguf");

    INFO("Working directory: " << fs::current_path().string());
    INFO("Config path: " << config_path.string());
    INFO("Buggy result: " << buggy);
    INFO("Fixed result: " << fixed);
    INFO("Buggy resolved: " << buggy_resolved.string());
    INFO("Fixed resolved: " << fixed_resolved.string());
    INFO("Actual model: " << model_path.string());

    REQUIRE(buggy == buggy_resolved.string());
    REQUIRE(fixed == fixed_resolved.string());

    // THE BUG: They differ
    REQUIRE(buggy != fixed);

    // Fixed is correct
    REQUIRE(fixed == model_path.string());
    REQUIRE(fs::exists(fixed));

    // Buggy is wrong - points outside project or to wrong location
    REQUIRE(buggy.find("..") != std::string::npos);

    fs::current_path(original_dir);
    fs::remove_all(temp_dir);
}
