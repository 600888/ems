get_filename_component(EMS_BACKEND_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

# handler 是独立的应用层；单独构建时先创建协议插件和 ABI 加载器。
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.21)
    project(ems_handlers LANGUAGES C CXX)
    include("${EMS_BACKEND_ROOT}/cmake/OutputDirectories.cmake")
    ems_set_output_directories("${CMAKE_BINARY_DIR}")
    option(BUILD_TESTS "Build handler and protocol integration tests" ON)
    enable_testing()
    add_subdirectory("../protocol" "protocol")
endif()

# 所有协议共用 ABI 适配、结果转换和实例管理，不链接任一协议栈。
add_library(
    net_handler STATIC
    plugin_handler.cpp handler_factory.cpp modbus/modbus_proto_handler.cpp
    iec104/iec104_handler.cpp dlt645/dlt645_handler.cpp dlt698/dlt698_handler.cpp
)

target_include_directories(net_handler PUBLIC "${EMS_BACKEND_ROOT}/src")

target_include_directories(
    net_handler SYSTEM
    PUBLIC "${EMS_BACKEND_ROOT}/third_party/libmodbuspp/3rdparty/json/single_include"
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

if(BUILD_TESTS)
    if(TARGET test_iec61850_runtime)
        target_link_libraries(test_iec61850_runtime PRIVATE net_handler)
        target_compile_definitions(test_iec61850_runtime PRIVATE EMS_IEC61850_HANDLER_TEST)
        add_test(NAME iec61850_handler COMMAND test_iec61850_runtime handler)
        set_tests_properties(iec61850_handler PROPERTIES TIMEOUT 30)
    endif()
    add_subdirectory("${EMS_BACKEND_ROOT}/tests/net/handler" "tests")
endif()
