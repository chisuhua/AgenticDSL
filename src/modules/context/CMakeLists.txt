add_library(agenticdsl_modules_context STATIC
    context_engine.cpp
    # ... 其他 context 源文件 ...
)
target_include_directories(agenticdsl_modules_context PUBLIC include)
target_link_libraries(agenticdsl_modules_context PUBLIC agenticdsl_common) # 链接 common 库
# ... 其他依赖 ...
