add_library(agenticdsl_modules_executor STATIC
    node_executor.cpp
    # ... 其他 executor 源文件 ...
)
target_include_directories(agenticdsl_modules_executor PUBLIC include)
target_link_libraries(agenticdsl_modules_executor PUBLIC agenticdsl_common) # 链接 common 库
# ... 其他依赖 (tools, llm) ...
