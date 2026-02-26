cmake_minimum_required(VERSION 3.20)
project(AgenticDSL LANGUAGES CXX)

option(AGENTICDSL_BUILD_TESTS "Build unit tests" OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

add_subdirectory(external/llama.cpp)
set(LLAMA_LIB llama)
set(LLAMA_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/external/llama.cpp/include")
set(GGML_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/external/llama.cpp/ggml/include")

# nlohmann_json (header-only)
find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp)
if(NOT NLOHMANN_JSON_INCLUDE_DIR)
    set(NLOHMANN_JSON_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/external/nlohmann_json/include")
endif()

# inja (header-only)
find_path(INJA_INCLUDE_DIR inja/inja.hpp)
if(NOT INJA_INCLUDE_DIR)
    set(INJA_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/external/inja/include")
endif()

set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(external/yaml-cpp yaml-cpp-build)
set(YAML_CPP_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/external/yaml-cpp/include")

include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/modules
    ${NLOHMANN_JSON_INCLUDE_DIR}
    ${INJA_INCLUDE_DIR}
    ${LLAMA_INCLUDE_DIR}
    ${GGML_INCLUDE_DIR}
    ${YAML_CPP_INCLUDE_DIR}
)

# 添加 llama 库路径
link_directories(${CMAKE_BINARY_DIR}/bin)

add_library(agenticdsl_common STATIC
    src/common/llm/llama_adapter.cpp
    src/common/tools/registry.cpp
    src/common/utils/parser_utils.cpp
    src/common/utils/template_renderer.cpp
    src/common/utils/yaml_json.cpp
)
target_link_libraries(agenticdsl_common PUBLIC
    yaml-cpp::yaml-cpp
    ${LLAMA_LIB}
)

add_subdirectory(src/modules/parser)
add_subdirectory(src/modules/context)
add_subdirectory(src/modules/budget)
add_subdirectory(src/modules/scheduler)
add_subdirectory(src/modules/executor)
add_subdirectory(src/modules/trace)
add_subdirectory(src/modules/library)
add_subdirectory(src/modules/system)

add_library(agenticdsl_core STATIC
    src/core/engine.cpp
)

target_link_libraries(agenticdsl_core
    agenticdsl_common
    agenticdsl_modules_parser
    agenticdsl_modules_context
    agenticdsl_modules_budget
    agenticdsl_modules_executor
    agenticdsl_modules_scheduler
    agenticdsl_modules_trace
    agenticdsl_modules_library
    agenticdsl_modules_system
    Threads::Threads
)

# --- Sample: agent_basic ---
add_executable(agent_basic
    examples/agent_basic/main.cpp
)

target_link_libraries(agent_basic
    agenticdsl_core
)

# --- Tests (Optional) ---
if(AGENTICDSL_BUILD_TESTS)
    add_subdirectory(tests)
endif()
