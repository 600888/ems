# Net Examples - 数据采集示例
#
# 展示 EMS 数据采集全流程:
#   1. 数据库加载通道/设备/测点配置
#   2. 采集引擎初始化
#   3. 插件加载与通道启动
#   4. 采集循环与数据分发

add_executable(data_collect_example data_collect_example.cpp)

target_include_directories(
    data_collect_example
    PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/third_party/libmodbuspp/include
            ${CMAKE_SOURCE_DIR}/third_party/libmodbuspp/3rdparty/json/single_include
)

target_link_libraries(
    data_collect_example
    PRIVATE net_collect
            net_para_loader
            ems_database_mysql
            ems_database_configuration
            rtdb
            ems_models
            redis++
            Threads::Threads
)

set_target_properties(data_collect_example PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES)

# 统一运行目录后，按单配置 / 多配置布局定位协议 DLL 或共享库。
if(CMAKE_CONFIGURATION_TYPES)
    target_compile_definitions(
        data_collect_example PRIVATE EMS_EXAMPLE_PLUGIN_RELATIVE_DIR="../../../lib/$<CONFIG>"
    )
else()
    target_compile_definitions(
        data_collect_example PRIVATE EMS_EXAMPLE_PLUGIN_RELATIVE_DIR="../../lib"
    )
endif()

message(STATUS "EMS net examples configured")

ems_copy_mysql_runtime(data_collect_example)

ems_copy_directory_runtime_dependencies()
