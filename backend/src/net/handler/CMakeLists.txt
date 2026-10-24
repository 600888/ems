get_filename_component(EMS_BACKEND_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

# handler 是独立的应用层；单独构建时先创建协议插件和 ABI 加载器。
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.15)
    project(ems_handlers LANGUAGES C CXX)
    option(BUILD_TESTS "Build handler and protocol integration tests" ON)
    enable_testing()
    add_subdirectory("../protocol" "protocol")
endif()

# 所有协议共用 ABI 适配、结果转换和实例管理，不链接任一协议栈。
add_library(
    net_handler STATIC plugin_handler.cpp handler_factory.cpp modbus/modbus_proto_handler.cpp
                       iec104/iec104_handler.cpp dlt645/dlt645_handler.cpp dlt698/dlt698_handler.cpp
)

target_include_directories(net_handler PUBLIC "${EMS_BACKEND_ROOT}/src")

target_include_directories(
    net_handler SYSTEM PUBLIC "${EMS_BACKEND_ROOT}/third_party/libmodbuspp/3rdparty/json/single_include"
)

target_link_libraries(net_handler PUBLIC ems_protocol_runtime)

set_target_properties(
    net_handler
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
               POSITION_INDEPENDENT_CODE ON
)

if(MSVC)
    target_compile_options(net_handler PRIVATE /utf-8)
endif()

# 存量 Modbus 设备处理器保留原接口；仅在 libmodbuspp 已启用时构建。
if(TARGET modbus_protocol)
    add_library(net_proto_handler STATIC base_handler.cpp modbus/modbus_handler.cpp)

    target_include_directories(
        net_proto_handler PRIVATE "${EMS_BACKEND_ROOT}/src" "${EMS_BACKEND_ROOT}/include"
    )

    target_include_directories(
        net_proto_handler SYSTEM
        PRIVATE "${EMS_BACKEND_ROOT}/third_party/libmodbuspp/3rdparty/json/single_include"
    )

    target_link_libraries(net_proto_handler PUBLIC modbus_protocol Threads::Threads)

    set_target_properties(
        net_proto_handler
        PROPERTIES CXX_STANDARD 20
                   CXX_STANDARD_REQUIRED YES
                   POSITION_INDEPENDENT_CODE ON
    )
endif()

if(BUILD_TESTS)
    add_subdirectory("${EMS_BACKEND_ROOT}/tests/net/handler" "tests")
endif()
