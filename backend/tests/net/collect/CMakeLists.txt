# 通道使用统一 handler 创建服务端，验证命令、真实读取、重启和退出后的租约释放。
add_executable(test_channel_handlers ../handler/protocol_handlers_test.cpp)

target_compile_definitions(test_channel_handlers PRIVATE EMS_CHANNEL_TESTS)
target_link_libraries(test_channel_handlers PRIVATE net_collect)

set_target_properties(test_channel_handlers PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES)

if(MSVC)
    target_compile_options(test_channel_handlers PRIVATE /utf-8)
endif()

if(WIN32)
    target_link_libraries(test_channel_handlers PRIVATE ws2_32)
endif()

foreach(protocol IN ITEMS modbus iec104 dlt645 dlt698)
    if(TARGET proto_${protocol})
        add_dependencies(test_channel_handlers proto_${protocol})
        target_compile_definitions(
            test_channel_handlers
            PRIVATE EMS_HANDLER_${protocol}_PLUGIN="$<TARGET_FILE:proto_${protocol}>"
        )
        add_test(NAME channel_${protocol}_handler COMMAND test_channel_handlers channel_${protocol})
        set_tests_properties(channel_${protocol}_handler PROPERTIES TIMEOUT 30)
    endif()
endforeach()

add_test(NAME channel_handler_failure COMMAND test_channel_handlers channel_failure)
set_tests_properties(channel_handler_failure PROPERTIES TIMEOUT 30)

if(TARGET proto_modbus AND TARGET proto_iec104 AND EMS_REALTIME_WITH_SHM)
    add_executable(test_connection_reuse connection_reuse_test.cpp)
    target_link_libraries(test_connection_reuse PRIVATE net_collect net_para_loader ems_libmodbus ems_lib60870)
    target_compile_definitions(test_connection_reuse PRIVATE EMS_REUSE_MODBUS_PLUGIN="$<TARGET_FILE:proto_modbus>")
    set_target_properties(test_connection_reuse PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES)
    if(MSVC)
        target_compile_options(test_connection_reuse PRIVATE /utf-8)
    endif()
    add_dependencies(test_connection_reuse proto_modbus proto_iec104)
    foreach(scenario IN ITEMS modbus iec104 settings endpoint siblings unit unit_boundary iec104_siblings)
        add_test(NAME connection_reuse_${scenario} COMMAND test_connection_reuse ${scenario})
        set_tests_properties(connection_reuse_${scenario} PROPERTIES TIMEOUT 30)
    endforeach()
endif()

ems_copy_directory_runtime_dependencies()

if(TARGET proto_iec61850 AND EMS_REALTIME_WITH_SHM)
    add_executable(test_iec61850_forwarding iec61850_forwarding_test.cpp)
    target_link_libraries(test_iec61850_forwarding PRIVATE net_collect net_para_loader ems_libiec61850)
    target_compile_definitions(test_iec61850_forwarding PRIVATE EMS_FORWARDING_PLUGIN="$<TARGET_FILE:proto_iec61850>")
    set_target_properties(test_iec61850_forwarding PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES)
    add_dependencies(test_iec61850_forwarding proto_iec61850)
    if(MSVC)
        target_compile_options(test_iec61850_forwarding PRIVATE /utf-8)
    endif()
    add_test(NAME iec61850_rtdb_forwarding COMMAND test_iec61850_forwarding)
    set_tests_properties(iec61850_rtdb_forwarding PROPERTIES TIMEOUT 45)
    ems_copy_directory_runtime_dependencies()
endif()
