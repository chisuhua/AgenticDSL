add_library(agenticdsl_modules_budget STATIC
    budget_controller.cpp
    # ... 其他 budget 源文件 ...
)
target_include_directories(agenticdsl_modules_budget PUBLIC include)
target_link_libraries(agenticdsl_modules_budget PUBLIC agenticdsl_common) # 链接 common 库
# ... 其他依赖 ...
