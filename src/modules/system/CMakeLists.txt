add_library(agenticdsl_modules_system STATIC
    system_nodes.cpp
    # ... 其他 library 源文件 ...
)
target_include_directories(agenticdsl_modules_system PUBLIC include)
target_link_libraries(agenticdsl_modules_system PUBLIC agenticdsl_common) # 链接 common 库
# ... 其他依赖 (parser) ...
