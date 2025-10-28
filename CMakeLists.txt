cmake_minimum_required(VERSION 3.20)
project(AgenticDSL LANGUAGES CXX)

option(AGENTICDSL_BUILD_TESTS "Build unit tests" OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

add_subdirectory(external/llama.cpp)
set(LLAMA_LIB llama) # llama.cpp 的 CMakeLists.txt 通常会创建名为 llama 的目标

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

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${NLOHMANN_JSON_INCLUDE_DIR}
    ${INJA_INCLUDE_DIR}
    ${LLAMA_INCLUDE_DIR}
)

#add_subdirectory(external/inja)
#nclude_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
#include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/nlohmann_json/single_include)
# 源文件
file(GLOB_RECURSE SOURCES "src/*.cpp")

# 创建库
add_library(agenticdsl SHARED ${SOURCES})
target_link_libraries(agenticdsl PRIVATE
    ${LLAMA_LIB} # 链接到由 external/llama.cpp 创建的目标
    Threads::Threads
    yaml-cpp
)

#target_compile_options(agenticdsl PRIVATE
#    $<$<CXX_COMPILER_ID:GNU,Clang>:-O3 -march=native -mtune=native>
#    $<$<CXX_COMPILER_ID:MSVC>:/O2>
#)

add_subdirectory(tests)

# 示例程序
#add_executable(agent_example examples/agent_example.cpp)
#target_link_libraries(agent_example agenticdsl)
#if(AGENTICDSL_BUILD_TESTS)
#    # 收集所有 test_*.cpp（排除 main_test_runner.cpp 重复定义）
#    file(GLOB TEST_SOURCES "tests/test_*.cpp")
#    list(FILTER TEST_SOURCES EXCLUDE REGEX "tests/main_test_runner.cpp")
#
#    # 主测试 runner（唯一含 CATCH_CONFIG_MAIN）
#    add_executable(agenticdsl_tests
#        tests/main_test_runner.cpp
#        tests/catch_amalgamated.cpp
#        ${TEST_SOURCES}
#    )
#    target_include_directories(agenticdsl_tests PRIVATE 
#        ${CATCH2_INCLUDE_DIR}
#        ${PROJECT_SOURCE_DIR}/external/llama.cpp/include   # ← 关键：添加 llama 头文件路径
#        ${PROJECT_SOURCE_DIR}/external/llama.cpp/ggml/include   # ← 关键：添加 llama 头文件路径
#        ${NLOHMANN_JSON_INCLUDE_DIR}
#        ${INJA_INCLUDE_DIR}
#        ${LLAMA_INCLUDE_DIR}
#    )
#    target_link_libraries(agenticdsl_tests PRIVATE agenticdsl)
#    #target_compile_definitions(agenticdsl_tests PRIVATE CATCH_CONFIG_ENABLE_ALL_STRINGMAKERS=1)
#
#    # 启用 ctest
#    enable_testing()
#    add_test(NAME agenticdsl_tests COMMAND agenticdsl_tests)
#endif()
