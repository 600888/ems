# net para_loader - 采集进程参数加载模块
#
# 注意：不作为独立库编译（非 STATIC/SHARED），
# 编译为 OBJECT 库跟随采集进程链接。
# 从公共配置仓储加载设备、通道及测点配置。
#
# 采集进程启动流程:
#   1. ParaLoader::load_all_as_config() 加载设备/通道/测点配置
#   2. 按通道创建协议处理器开始采集
#   3. 采样交给遥测/历史链路，不直接写关系库

add_library(net_para_loader OBJECT para_loader.cpp)

target_include_directories(
    net_para_loader
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src
           ${CMAKE_SOURCE_DIR}/3rd ${CMAKE_SOURCE_DIR}/3rd/libmodbuspp/3rdparty/json/single_include
)

target_link_libraries(net_para_loader PUBLIC ems_database_api Threads::Threads)

set_target_properties(
    net_para_loader
    PROPERTIES CXX_STANDARD 20
               CXX_STANDARD_REQUIRED YES
               POSITION_INDEPENDENT_CODE ON
)

message(STATUS "EMS net_para_loader module configured (OBJECT)")
