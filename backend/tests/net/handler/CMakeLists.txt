# 从应用层处理器经真实 ABI 加载协议插件，验证每种协议均可独立调用。
add_executable(test_protocol_handlers protocol_handlers_test.cpp)

target_include_directories(test_protocol_handlers PRIVATE "${EMS_BACKEND_ROOT}/src")

target_include_directories(
    test_protocol_handlers SYSTEM
    PRIVATE "${EMS_BACKEND_ROOT}/3rd/libmodbuspp/3rdparty/json/single_include"
)

target_link_libraries(test_protocol_handlers PRIVATE net_handler)

set_target_properties(
    test_protocol_handlers
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
)

if(MSVC)
    target_compile_options(test_protocol_handlers PRIVATE /utf-8)
endif()

if(WIN32)
    target_link_libraries(test_protocol_handlers PRIVATE ws2_32)
endif()

foreach(protocol IN ITEMS modbus iec104 dlt645 dlt698)
    if(TARGET proto_${protocol})
        add_dependencies(test_protocol_handlers proto_${protocol})
        target_compile_definitions(
            test_protocol_handlers
            PRIVATE EMS_HANDLER_${protocol}_PLUGIN="$<TARGET_FILE:proto_${protocol}>"
        )
        add_test(NAME handler_${protocol} COMMAND test_protocol_handlers ${protocol})
        set_tests_properties(handler_${protocol} PROPERTIES TIMEOUT 30)
    endif()
endforeach()

add_test(NAME handler_factory_and_failure COMMAND test_protocol_handlers factory)
set_tests_properties(handler_factory_and_failure PROPERTIES TIMEOUT 30)
