# 动态协议插件集成测试，覆盖 ABI、实例生命周期和网络读写。

if(TARGET proto_modbus)
    add_executable(test_protocol_runtime protocol_runtime_test.cpp)

    target_include_directories(
        test_protocol_runtime
        PRIVATE "${EMS_BACKEND_ROOT}/3rd/libmodbuspp/3rdparty/json/single_include"
    )

    target_link_libraries(
        test_protocol_runtime PRIVATE ems_protocol_runtime ems_libmodbus Threads::Threads
    )

    set_target_properties(
        test_protocol_runtime
        PROPERTIES CXX_STANDARD 17
                   CXX_STANDARD_REQUIRED YES
                   CXX_EXTENSIONS NO
    )

    foreach(kind IN ITEMS normal bad_abi null_create missing_symbol)
        add_library(protocol_fixture_${kind} MODULE plugin_fixture.cpp)

        target_include_directories(protocol_fixture_${kind} PRIVATE "${EMS_BACKEND_ROOT}/include")

        target_compile_definitions(protocol_fixture_${kind} PRIVATE EMS_BUILD_PROTOCOL_PLUGIN)

        set_target_properties(
            protocol_fixture_${kind}
            PROPERTIES CXX_STANDARD 17
                       CXX_STANDARD_REQUIRED YES
                       CXX_EXTENSIONS NO
                       LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fixtures"
                       RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fixtures"
        )
    endforeach()

    target_compile_definitions(protocol_fixture_bad_abi PRIVATE EMS_TEST_BAD_ABI)

    target_compile_definitions(protocol_fixture_null_create PRIVATE EMS_TEST_NULL_CREATE)

    target_compile_definitions(protocol_fixture_missing_symbol PRIVATE EMS_TEST_MISSING_SYMBOL)
    add_dependencies(
        test_protocol_runtime proto_modbus protocol_fixture_normal protocol_fixture_bad_abi
        protocol_fixture_null_create protocol_fixture_missing_symbol
    )

    target_compile_definitions(
        test_protocol_runtime
        PRIVATE EMS_REAL_PLUGIN="$<TARGET_FILE:proto_modbus>"
                EMS_NORMAL_PLUGIN="$<TARGET_FILE:protocol_fixture_normal>"
                EMS_BAD_ABI_PLUGIN="$<TARGET_FILE:protocol_fixture_bad_abi>"
                EMS_NULL_PLUGIN="$<TARGET_FILE:protocol_fixture_null_create>"
                EMS_MISSING_PLUGIN="$<TARGET_FILE:protocol_fixture_missing_symbol>"
    )

    foreach(
        scenario IN
        ITEMS lazy_load
              aliases
              concurrent_load
              failures
              client_types
              client_writes
              server_readonly
              buffer_retry
              rtu_config
              legacy_config
              batch_boundaries
              failed_stop
              instance_routing
    )

        add_test(NAME protocol_${scenario} COMMAND test_protocol_runtime ${scenario})
        set_tests_properties(protocol_${scenario} PROPERTIES TIMEOUT 30)
    endforeach()

    if(EMS_PROTOCOL_RUNTIME_DEPENDENCIES)
        add_custom_command(
            TARGET test_protocol_runtime
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EMS_PROTOCOL_RUNTIME_DEPENDENCIES}
                    "$<TARGET_FILE_DIR:test_protocol_runtime>"
            COMMAND_EXPAND_LISTS
        )
    endif()

endif()

if(TARGET proto_iec104)
    add_executable(test_iec104_runtime iec104_runtime_test.cpp)

    target_include_directories(
        test_iec104_runtime SYSTEM
        PRIVATE "${EMS_BACKEND_ROOT}/3rd/libmodbuspp/3rdparty/json/single_include"
    )

    target_link_libraries(
        test_iec104_runtime PRIVATE ems_protocol_runtime ems_lib60870 Threads::Threads
    )

    set_target_properties(
        test_iec104_runtime
        PROPERTIES CXX_STANDARD 17
                   CXX_STANDARD_REQUIRED YES
                   CXX_EXTENSIONS NO
    )

    target_compile_definitions(
        test_iec104_runtime PRIVATE EMS_IEC104_PLUGIN="$<TARGET_FILE:proto_iec104>"
    )
    add_dependencies(test_iec104_runtime proto_iec104)

    foreach(
        scenario IN
        ITEMS lazy_load
              config
              monitoring_types
              client_cache
              time_and_window
              commands
              command_timeout
              server
              buffer_retry
              reconnect
              concurrent_instances
    )

        add_test(NAME iec104_${scenario} COMMAND test_iec104_runtime ${scenario})
        set_tests_properties(iec104_${scenario} PROPERTIES TIMEOUT 30)
    endforeach()

    if(EMS_PROTOCOL_RUNTIME_DEPENDENCIES)
        add_custom_command(
            TARGET test_iec104_runtime
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EMS_PROTOCOL_RUNTIME_DEPENDENCIES}
                    "$<TARGET_FILE_DIR:test_iec104_runtime>"
            COMMAND_EXPAND_LISTS
        )
    endif()
endif()
