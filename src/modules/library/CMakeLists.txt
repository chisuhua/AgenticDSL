add_library(agenticdsl_modules_library STATIC
    library_loader.cpp
    # ... 其他 library 源文件 ...
)
target_include_directories(agenticdsl_modules_library PUBLIC include)
target_link_libraries(agenticdsl_modules_library PUBLIC agenticdsl_common) # 链接 common 库
# ... 其他依赖 (parser) ...
