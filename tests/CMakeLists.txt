# 启用测试支持
enable_testing()

# Catch2 单头文件路径
set(CATCH_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# 定义创建单个 Catch2 测试的函数
function(add_catch_test TEST_NAME)
    # 参数：测试名称，其余为该测试的源文件（不含 catch_amalgamated.cpp）
    set(SOURCES ${ARGN})

    # 自动包含 catch_amalgamated.cpp
    list(APPEND SOURCES ${CATCH_INCLUDE_DIR}/catch_amalgamated.cpp main_test_runner.cpp)

    # 创建可执行文件
    add_executable(${TEST_NAME} ${SOURCES})

    # 包含目录
    target_include_directories(${TEST_NAME} PRIVATE
        ${CATCH_INCLUDE_DIR}
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/external/llama.cpp/include   # ← 关键：添加 llama 头文件路径
        ${PROJECT_SOURCE_DIR}/external/llama.cpp/ggml/include   # ← 关键：添加 llama 头文件路径
        ${PROJECT_SOURCE_DIR}/external/nlohmann_json/single_include
        ${PROJECT_SOURCE_DIR}/external/inja/include
        ${PROJECT_SOURCE_DIR}/external/yaml-cpp/include
    )

    target_link_libraries(${TEST_NAME} PRIVATE agenticdsl yaml-cpp::yaml-cpp)   # 或你内部构建的 yaml-cpp 目标
    target_compile_definitions(${TEST_NAME} PRIVATE CATCH_CONFIG_ENABLE_ALL_STRINGMAKERS=1)

    # 注册为 CTest 测试
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# ================================
# 方案一：为每个 test_xxx.cpp 创建独立测试（推荐）
# ================================

file(GLOB SINGLE_TEST_SOURCES "test_*.cpp")
# 列出所有独立的测试源文件（每个文件一个测试）
#set(SINGLE_TEST_SOURCES
#    test_basic.cpp
#    test_engine.cpp
#    test_parser.cpp
#    test_scheduler.cpp
#)

# 为每个源文件创建一个独立的测试
foreach(TEST_SRC ${SINGLE_TEST_SOURCES})
    # 提取测试名，如 test_reg.cpp -> test_reg
    get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
    add_catch_test(${TEST_NAME} ${TEST_SRC})
endforeach()

