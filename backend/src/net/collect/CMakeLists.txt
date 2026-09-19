# net/collect - 采集引擎模块
#
# 包含采集引擎主类、通道线程、数据分发器和插件管理器。
# 编译为 STATIC 库，由采集进程主程序链接。
#
# 依赖:
#   - ems_rtdb  (RTDB 实时库)
#   - pthread   (线程)
#   - dl        (dlopen/dlsym)
#   - nlohmann_json (JSON 解析)

add_library(net_collect STATIC
    collect_engine.cpp
    channel_thread.cpp
    data_dispatcher.cpp
    plugin_manager.cpp
)

target_include_directories(net_collect PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/3rd
    ${CMAKE_SOURCE_DIR}/3rd/libmodbuspp/3rdparty/json/single_include
)

target_link_libraries(net_collect PUBLIC
    rtdb
    pthread
    dl
)

set_target_properties(net_collect PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    POSITION_INDEPENDENT_CODE ON
)

message(STATUS "EMS net_collect module configured (STATIC)")
