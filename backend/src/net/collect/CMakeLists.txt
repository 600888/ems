# net/collect - 采集引擎模块
#
# 包含采集引擎主类、通道线程、数据分发器。
# 编译为 STATIC 库，由采集进程主程序链接。
#
# 依赖:
#   - rtdb (RTDB 实时库)
#   - Threads::Threads (平台线程)
#   - net_handler (统一处理器与平台动态库加载)
#   - nlohmann_json (JSON 解析)

add_library(net_collect STATIC collect_engine.cpp channel_thread.cpp data_dispatcher.cpp)

target_include_directories(
    net_collect
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include
           ${CMAKE_SOURCE_DIR}/third_party
           ${CMAKE_SOURCE_DIR}/third_party/libmodbuspp/3rdparty/json/single_include
)

target_link_libraries(net_collect PUBLIC ems::rtdb Threads::Threads net_handler)
if(TARGET redis++)
    target_link_libraries(net_collect PRIVATE redis++)
    target_compile_definitions(net_collect PRIVATE EMS_NOTIFICATION_WITH_REDIS)
endif()

if(EMS_WITH_ZEROMQ)
    target_link_libraries(net_collect PUBLIC ems_zmq_messaging)

    target_compile_definitions(net_collect PRIVATE EMS_WITH_ZEROMQ)
endif()

set_target_properties(
    net_collect
    PROPERTIES CXX_STANDARD 20
               CXX_STANDARD_REQUIRED YES
               POSITION_INDEPENDENT_CODE ON
)

message(STATUS "EMS net_collect module configured (STATIC)")
