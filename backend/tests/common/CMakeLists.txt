# 公共模块单元测试（docs/common/design.md 第 15/16 节 P1 完成门槛：
# 多对多、树/引用/类型校验、无效值和 64 位整数往返通过）
if(NOT BUILD_TESTS)
    return()
endif()

include(GoogleTest)

add_executable(
    test_ems_common test_core.cpp test_model.cpp test_device_config.cpp test_point_table.cpp
                    test_codec.cpp test_realtime.cpp
)

target_include_directories(
    test_ems_common
    PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/3rd/googletest/googletest/include
            ${CMAKE_SOURCE_DIR}/3rd/googletest/googlemock/include
)

target_link_libraries(
    test_ems_common
    PRIVATE gtest
            gmock
            Threads::Threads
            ems_common_codec
            ems_common_model
            ems_common_core
            ems_messaging_api
            ems_realtime_core
            ems_config_core
)

# 公共模块目标要求 C++17（design.md 第 4 节）
set_target_properties(
    test_ems_common
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
)

gtest_discover_tests(test_ems_common)

if(EMS_WITH_ZEROMQ)
    add_executable(test_ems_zmq_messaging test_zmq_messaging.cpp)

    target_include_directories(
        test_ems_zmq_messaging PRIVATE ${CMAKE_SOURCE_DIR}/include
                                       ${CMAKE_SOURCE_DIR}/3rd/googletest/googletest/include
    )

    target_link_libraries(test_ems_zmq_messaging PRIVATE ems_zmq_messaging gtest Threads::Threads)

    set_target_properties(
        test_ems_zmq_messaging
        PROPERTIES CXX_STANDARD 17
                   CXX_STANDARD_REQUIRED YES
                   CXX_EXTENSIONS NO
    )

    add_test(NAME zmq_messaging COMMAND test_ems_zmq_messaging)
endif()

message(STATUS "Created test_ems_common executable")
