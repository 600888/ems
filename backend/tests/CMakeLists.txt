# 测试继承后端的编译器、标准和依赖配置，不重新覆盖全局编译参数。
include_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}/../include" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/libmodbuspp/3rdparty/json/single_include"
)

add_subdirectory(common)
add_subdirectory(rtdb)
add_subdirectory(util)
add_subdirectory(db)

# 存量处理器测试依赖 libmodbuspp；动态协议插件测试在协议模块内注册。
if(TARGET modbus_protocol)
    add_subdirectory(net)
endif()

if(TARGET net_collect)
    add_subdirectory(net/collect)
endif()

add_subdirectory(storage)

if(EMS_BUILD_WEB)
    add_subdirectory(web)
endif()
