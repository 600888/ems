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
        target_compile_definitions(test_channel_handlers PRIVATE EMS_HANDLER_${protocol}_PLUGIN="$<TARGET_FILE:proto_${protocol}>")
        add_test(NAME channel_${protocol}_handler COMMAND test_channel_handlers channel_${protocol})
        set_tests_properties(channel_${protocol}_handler PROPERTIES TIMEOUT 30)
    endif()
endforeach()

add_test(NAME channel_handler_failure COMMAND test_channel_handlers channel_failure)
set_tests_properties(channel_handler_failure PROPERTIES TIMEOUT 30)
