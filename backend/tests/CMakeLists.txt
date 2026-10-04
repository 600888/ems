# 测试继承后端的编译器、标准和依赖配置，不重新覆盖全局编译参数。
include_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}/../include" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../3rd/libmodbuspp/3rdparty/json/single_include"
)

add_subdirectory(common)

# 存量处理器测试依赖 libmodbuspp；动态协议插件测试在协议模块内注册。
if(TARGET modbus_protocol)
    add_subdirectory(net)
endif()

if(TARGET net_collect)
    add_subdirectory(net/collect)
endif()

add_subdirectory(storage)
