# net/protocol/handler - 协议处理器抽象基类
#
# 定义规约处理器的统一接口 (ProtocolHandler/ServerHandler/ClientHandler)
# 和通用数据结构 (ProtocolPoint, ValueType, MessageCapture 等)。
# 各协议的具体实现位于各自的子目录 (如 protocol/modbus/)。
#
# 以 OBJECT 库形式供采集进程和协议插件共享。

add_library(net_proto_handler OBJECT
    base_handler.cpp
)

target_include_directories(net_proto_handler PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/inc
    ${CMAKE_SOURCE_DIR}/3rd/libmodbuspp/3rdparty/json/single_include
)

target_link_libraries(net_proto_handler PUBLIC
    Threads::Threads
)

set_target_properties(net_proto_handler PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    POSITION_INDEPENDENT_CODE ON
)

message(STATUS "EMS net_proto_handler configured (OBJECT)")
