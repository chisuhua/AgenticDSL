add_library(agenticdsl_modules_scheduler STATIC
    topo_scheduler.cpp
    execution_session.cpp # v3.1
    resource_manager.cpp # v3.1
    # ... 其他 scheduler 源文件 ...
)
target_include_directories(agenticdsl_modules_scheduler PUBLIC include)
target_link_libraries(agenticdsl_modules_scheduler PUBLIC 
    agenticdsl_common
    agenticdsl_modules_context
    agenticdsl_modules_budget
    agenticdsl_modules_executor
    agenticdsl_modules_trace
)
