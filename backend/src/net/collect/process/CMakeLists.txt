add_executable(ems-collector main.cpp)
target_link_libraries(ems-collector PRIVATE net_collect net_para_loader ems_database_mysql)
set_target_properties(ems-collector PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES)

# 可执行文件与协议插件在不同目录，多配置构建还需跨过配置名这一层。
if(CMAKE_CONFIGURATION_TYPES)
    target_compile_definitions(
        ems-collector PRIVATE EMS_COLLECTOR_PLUGIN_RELATIVE_DIR="../../lib/$<CONFIG>"
    )
else()
    target_compile_definitions(ems-collector PRIVATE EMS_COLLECTOR_PLUGIN_RELATIVE_DIR="../lib")
endif()
foreach(protocol IN ITEMS modbus iec104 dlt645 dlt698)
    if(TARGET proto_${protocol})
        add_dependencies(ems-collector proto_${protocol})
    endif()
endforeach()

ems_copy_mysql_runtime(ems-collector)
ems_copy_directory_runtime_dependencies()

if(BUILD_TESTS)
    add_test(NAME collector_help COMMAND ems-collector --help)
    add_test(NAME collector_check_config
             COMMAND ems-collector --config "${CMAKE_SOURCE_DIR}/conf/collector.json"
                     --check-config
    )
    add_test(NAME collector_rejects_unknown_argument COMMAND ems-collector --unknown-option)
    add_test(NAME collector_rejects_missing_config_argument COMMAND ems-collector --config)
    set_tests_properties(
        collector_rejects_unknown_argument collector_rejects_missing_config_argument
        PROPERTIES WILL_FAIL TRUE
    )
endif()
