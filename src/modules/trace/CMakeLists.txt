add_library(agenticdsl_modules_trace STATIC
    trace_exporter.cpp
    # ... 其他 trace 源文件 ...
)
target_include_directories(agenticdsl_modules_trace PUBLIC include)
target_link_libraries(agenticdsl_modules_trace PUBLIC agenticdsl_common) # 链接 common 库
# ... 其他依赖 ...
