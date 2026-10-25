# 公共模块单元测试（website/docs/common/design.md 第 15/16 节 P1 完成门槛：
# 多对多、树/引用/类型校验、无效值和 64 位整数往返通过）
if(NOT BUILD_TESTS)
    return()
endif()

include(GoogleTest)

add_executable(
    test_ems_common
    test_core.cpp
    test_model.cpp
    test_device_config.cpp
    test_point_table.cpp
    test_codec.cpp
    test_realtime.cpp
    test_realtime_service.cpp
)

target_include_directories(
    test_ems_common
    PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/third_party/googletest/googletest/include
            ${CMAKE_SOURCE_DIR}/third_party/googletest/googlemock/include
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
            ems_excel
)

# 公共模块目标要求 C++17（design.md 第 4 节）
set_target_properties(
    test_ems_common
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
)

add_executable(test_ems_database_runtime test_database_runtime.cpp)
target_include_directories(test_ems_database_runtime PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(
    test_ems_database_runtime SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/ormpp
)
target_link_libraries(
    test_ems_database_runtime PRIVATE ems_database_runtime gtest_main Threads::Threads
)
set_target_properties(
    test_ems_database_runtime
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
)
get_target_property(_ems_test_mysql ems_database_mysql EMS_MYSQL_AVAILABLE)
if(_ems_test_mysql)
    add_executable(test_ems_database_mysql test_database_mysql.cpp)
    target_include_directories(
        test_ems_database_mysql SYSTEM PRIVATE ${EMS_DATABASE_MYSQL_INCLUDE_DIR}
                                               ${CMAKE_SOURCE_DIR}/third_party/ormpp
    )
    target_compile_definitions(test_ems_database_mysql PRIVATE ORMPP_ENABLE_MYSQL)
    target_link_libraries(test_ems_database_mysql PRIVATE ems_database_mysql gtest_main)
    set_target_properties(
        test_ems_database_mysql
        PROPERTIES CXX_STANDARD 17
                   CXX_STANDARD_REQUIRED YES
                   CXX_EXTENSIONS NO
    )
    ems_copy_mysql_runtime(test_ems_database_mysql)
endif()

if(EMS_WITH_ZEROMQ)
    add_executable(test_ems_zmq_messaging test_zmq_messaging.cpp)

    target_include_directories(
        test_ems_zmq_messaging
        PRIVATE ${CMAKE_SOURCE_DIR}/include
                ${CMAKE_SOURCE_DIR}/third_party/googletest/googletest/include
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

ems_copy_directory_runtime_dependencies()

gtest_discover_tests(test_ems_common)
gtest_discover_tests(test_ems_database_runtime)
if(TARGET test_ems_database_mysql)
    gtest_discover_tests(test_ems_database_mysql)
endif()
