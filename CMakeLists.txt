cmake_minimum_required(VERSION 3.20)
project(AgenticDSL LANGUAGES CXX)

option(AGENTICDSL_BUILD_TESTS "Build unit tests" ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

add_subdirectory(external/llama.cpp)
#set(LLAMA_LIB llama) # llama.cpp 的 CMakeLists.txt 通常会创建名为 llama 的目标

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
add_library(agenticdsl STATIC ${SOURCES})
target_link_libraries(agenticdsl PRIVATE
    ${LLAMA_LIB} # 链接到由 external/llama.cpp 创建的目标
    Threads::Threads
)

#target_compile_options(agenticdsl PRIVATE
#    $<$<CXX_COMPILER_ID:GNU,Clang>:-O3 -march=native -mtune=native>
#    $<$<CXX_COMPILER_ID:MSVC>:/O2>
#)

# 示例程序
#add_executable(agent_example examples/agent_example.cpp)
#target_link_libraries(agent_example agenticdsl)
if(AGENTICDSL_BUILD_TESTS)
    # 收集所有 test_*.cpp（排除 main_test_runner.cpp 重复定义）
    file(GLOB TEST_SOURCES "tests/test_*.cpp")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "tests/main_test_runner.cpp")

    # 主测试 runner（唯一含 CATCH_CONFIG_MAIN）
    add_executable(agenticdsl_tests
        tests/main_test_runner.cpp
        ${TEST_SOURCES}
    )
    target_include_directories(agenticdsl_tests PRIVATE ${CATCH2_INCLUDE_DIR}
        ${LLAMA_INCLUDE_DIR}
    )
    target_link_libraries(agenticdsl_tests PRIVATE agenticdsl)
    target_compile_definitions(agenticdsl_tests PRIVATE CATCH_CONFIG_ENABLE_ALL_STRINGMAKERS=1)

    # 启用 ctest
    enable_testing()
    add_test(NAME AgenticDSL_Tests COMMAND agenticdsl_tests)
endif()
