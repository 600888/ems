# Net Examples - 数据采集示例
#
# 展示 EMS 数据采集全流程:
#   1. 数据库加载通道/设备/测点配置
#   2. 采集引擎初始化
#   3. 插件加载与通道启动
#   4. 采集循环与数据分发

find_package(PkgConfig REQUIRED)
pkg_check_modules(HIREDIS REQUIRED hiredis)

add_executable(data_collect_example
    data_collect_example.cpp
)

target_include_directories(data_collect_example PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/3rd/libmodbuspp/include
    ${CMAKE_SOURCE_DIR}/3rd/libmodbuspp/3rdparty/json/single_include
)

target_link_libraries(data_collect_example PRIVATE
    net_collect
    net_para_loader
    ems_db
    rtdb
    ems_models
    redis++
    pthread
    dl
    ${HIREDIS_LIBRARIES}
)

set_target_properties(data_collect_example PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
)

message(STATUS "EMS net examples configured")
