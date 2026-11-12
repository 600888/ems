function(dlt698_add_example target)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(${target} ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE ${ARG_LIBS})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/transport)
    dlt698_target_setup(${target})
endfunction()

# 编解码层：不依赖会话和对象服务，单进程离线运行。
dlt698_add_example(dlt698_decode SOURCES codec/decode.cpp LIBS dlt698)

# 业务层：内存通道上的对象目录、读写方法和记录查询，无需真实设备。
dlt698_add_example(dlt698_memory_get SOURCES service/memory_get.cpp LIBS dlt698)
dlt698_add_example(dlt698_memory_mutation SOURCES service/memory_mutation.cpp LIBS dlt698)
dlt698_add_example(dlt698_standard_points SOURCES service/standard_points.cpp LIBS dlt698)
dlt698_add_example(dlt698_standard_points_extended
    SOURCES service/standard_points_extended.cpp LIBS dlt698)
dlt698_add_example(dlt698_standard_records SOURCES service/standard_records.cpp LIBS dlt698)

if(DLT698_BUILD_TRANSPORT)
    # 普通入口：数据和启动由库托管，不在示例中组装会话或事件循环。
    dlt698_add_example(dlt698_server SOURCES app/server.cpp LIBS dlt698)
    dlt698_add_example(dlt698_client SOURCES app/client.cpp LIBS dlt698)
    # 传输层：真实 socket 和串口，服务端与客户端各一个独立程序。
    dlt698_add_example(dlt698_tcp_server SOURCES transport/tcp_server.cpp
        LIBS dlt698)
    dlt698_add_example(dlt698_tcp_client SOURCES transport/tcp_client.cpp
        LIBS dlt698)
    dlt698_add_example(dlt698_rtu_server SOURCES transport/rtu_server.cpp
        LIBS dlt698)
    dlt698_add_example(dlt698_rtu_client SOURCES transport/rtu_client.cpp
        LIBS dlt698)
    # 双向拨号：协议角色由程序决定，TCP 连接发起方向独立，用于验证该约定。
    dlt698_add_example(dlt698_master SOURCES transport/peer.cpp
        LIBS dlt698)
    add_executable(dlt698_terminal transport/peer.cpp)
    target_compile_definitions(dlt698_terminal PRIVATE DLT698_TERMINAL)
    target_include_directories(dlt698_terminal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/transport)
    target_link_libraries(dlt698_terminal PRIVATE dlt698)
    dlt698_target_setup(dlt698_terminal)
endif()
