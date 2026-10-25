add_executable(test_process_logger test_process_logger.cpp)
target_include_directories(test_process_logger PRIVATE "${CMAKE_SOURCE_DIR}/third_party")
target_link_libraries(test_process_logger PRIVATE Threads::Threads)
target_compile_features(test_process_logger PRIVATE cxx_std_17)

# 初始化器每个进程只运行一次，各场景使用独立进程验证。
foreach(scenario IN ITEMS trace partial off rotation missing malformed)
    add_test(NAME process_logger_${scenario} COMMAND test_process_logger ${scenario})
endforeach()

if(EMS_BUILD_WEB)
    add_executable(test_web_logging test_web_logging.cpp ../../src/web/web_log.cpp)
    target_include_directories(test_web_logging PRIVATE "${CMAKE_SOURCE_DIR}/third_party")
    target_link_libraries(test_web_logging PRIVATE drogon Threads::Threads)
    target_compile_features(test_web_logging PRIVATE cxx_std_17)
    add_test(NAME web_framework_logging COMMAND test_web_logging)
endif()

ems_copy_directory_runtime_dependencies()
