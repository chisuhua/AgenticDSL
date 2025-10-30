add_library(agenticdsl_modules_parser STATIC
    markdown_parser.cpp
    # ... 其他 parser 源文件 ...
)
target_include_directories(agenticdsl_modules_parser PUBLIC include)
target_link_libraries(agenticdsl_modules_parser PUBLIC agenticdsl_common) # 链接 common 库
# ... 其他依赖 ...
