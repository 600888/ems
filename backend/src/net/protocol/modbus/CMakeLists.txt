# Modbus Protocol Implementation
# Supports Modbus RTU (Serial) and Modbus TCP (Ethernet) communications
#
# 底层通信库 + 协议处理器 + 采集插件 (.so)

# ---- 底层通信库 ----
add_library(modbus_protocol STATIC
    modbus_client.cpp
    modbus_server.cpp
)

target_include_directories(modbus_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/3rd
    ${CMAKE_SOURCE_DIR}/3rd/libmodbuspp/include
    ${CMAKE_SOURCE_DIR}/inc
)

target_link_libraries(modbus_protocol PUBLIC
    ${LIBMODBUS_LIBRARIES}
    modbuspp-shared
    Threads::Threads
)

set_target_properties(modbus_protocol PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    POSITION_INDEPENDENT_CODE ON
)

# ---- 插件动态库 (MODULE) ----
add_library(proto_modbus MODULE
    modbus_handler.cpp
    modbus_plugin.cpp
    modbus_client.cpp
    modbus_server.cpp
    ${CMAKE_SOURCE_DIR}/src/net/protocol/handler/base_handler.cpp
)

target_include_directories(proto_modbus PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/inc
    ${CMAKE_SOURCE_DIR}/3rd
    ${CMAKE_SOURCE_DIR}/3rd/libmodbuspp/include
    ${CMAKE_SOURCE_DIR}/3rd/libmodbuspp/3rdparty/json/single_include
)

target_link_libraries(proto_modbus PRIVATE
    ${LIBMODBUS_LIBRARIES}
    modbuspp-shared
    Threads::Threads
    dl
)

set_target_properties(proto_modbus PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    PREFIX "lib"
)

message(STATUS "EMS proto_modbus plugin configured (MODULE → libproto_modbus.so)")
