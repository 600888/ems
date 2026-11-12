# 存储契约及适配器工厂测试，不要求连接外部数据库。

if(NOT BUILD_TESTS)
    return()
endif()

if(TARGET ems_timeseries_taos)
    add_executable(test_taos_live test_taos_live.cpp)
    target_include_directories(test_taos_live PRIVATE ${EMS_DATABASE_TAOS_INCLUDE_DIR})
    target_link_libraries(test_taos_live PRIVATE ems_timeseries ems_common_codec gtest_main)
    set_target_properties(test_taos_live PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
    ems_copy_taos_runtime(test_taos_live)
    if(TARGET ems_web_history)
        target_compile_definitions(test_taos_live PRIVATE EMS_TAOS_WEB_LIVE)
        target_link_libraries(test_taos_live PRIVATE ems_web_history)
        ems_copy_mysql_runtime(test_taos_live)
    endif()
    ems_copy_runtime_files(test_taos_live "$<TARGET_RUNTIME_DLLS:test_taos_live>")
    include(GoogleTest)
    gtest_discover_tests(test_taos_live WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        PROPERTIES LABELS integration)
endif()

add_executable(test_ems_storage test_storage.cpp)

target_include_directories(test_ems_storage PRIVATE ${CMAKE_SOURCE_DIR}/src/scd)

target_link_libraries(test_ems_storage PRIVATE ems_storage_core ems_common_codec)

set_target_properties(
    test_ems_storage
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
)

add_test(NAME ems_storage COMMAND test_ems_storage)

add_executable(
    test_ems_telemetry_feed test_zmq_telemetry.cpp ${CMAKE_SOURCE_DIR}/src/scd/zmq_telemetry.cpp
)

target_include_directories(test_ems_telemetry_feed PRIVATE ${CMAKE_SOURCE_DIR}/src/scd)

target_link_libraries(
    test_ems_telemetry_feed PRIVATE ems_common_codec ems_messaging_api ems_common_core
                                    Threads::Threads
)

set_target_properties(
    test_ems_telemetry_feed
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
)

add_test(NAME ems_telemetry_feed COMMAND test_ems_telemetry_feed)

add_executable(test_ems_time_series_factory test_time_series_factory.cpp)

target_include_directories(test_ems_time_series_factory PRIVATE ${CMAKE_SOURCE_DIR}/src/scd)

target_link_libraries(test_ems_time_series_factory PRIVATE ems_timeseries)

set_target_properties(
    test_ems_time_series_factory
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
)

add_test(NAME ems_time_series_factory COMMAND test_ems_time_series_factory)

# These contract executables use assert, including in Release builds.
foreach(_ems_test test_ems_storage test_ems_telemetry_feed test_ems_time_series_factory)
    if(MSVC)
        target_compile_options(${_ems_test} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${_ems_test} PRIVATE -UNDEBUG)
    endif()
endforeach()

ems_copy_directory_runtime_dependencies()

add_executable(test_storage_sampling test_sampling.cpp)
target_include_directories(test_storage_sampling PRIVATE ${CMAKE_SOURCE_DIR}/src/scd
    ${CMAKE_SOURCE_DIR}/third_party/libmodbuspp/3rdparty/json/single_include)
target_link_libraries(test_storage_sampling PRIVATE ems_storage_core ems_common_codec gtest_main)
set_target_properties(test_storage_sampling PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
include(GoogleTest)
gtest_discover_tests(test_storage_sampling)

if(TARGET ems_storage_native)
    add_executable(test_storage_options test_storage_options.cpp)
    target_include_directories(test_storage_options PRIVATE ${CMAKE_SOURCE_DIR}/src/scd
        ${CMAKE_SOURCE_DIR}/third_party/libmodbuspp/3rdparty/json/single_include)
    target_link_libraries(test_storage_options PRIVATE ems_storage_native gtest_main)
    set_target_properties(test_storage_options PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
    ems_copy_mysql_runtime(test_storage_options)
    gtest_discover_tests(test_storage_options)
endif()
