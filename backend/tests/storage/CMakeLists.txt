if(NOT BUILD_TESTS)
    return()
endif()

add_executable(test_ems_storage test_storage.cpp)
target_include_directories(test_ems_storage PRIVATE ${CMAKE_SOURCE_DIR}/src/scd)
target_link_libraries(test_ems_storage PRIVATE ems_storage_core ems_common_codec)
set_target_properties(test_ems_storage PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
add_test(NAME ems_storage COMMAND test_ems_storage)

add_executable(test_ems_telemetry_feed
    test_zmq_telemetry.cpp ${CMAKE_SOURCE_DIR}/src/scd/zmq_telemetry.cpp)
target_include_directories(test_ems_telemetry_feed PRIVATE ${CMAKE_SOURCE_DIR}/src/scd)
target_link_libraries(test_ems_telemetry_feed PRIVATE ems_common_codec ems_messaging_api
                                                   ems_common_core Threads::Threads)
set_target_properties(test_ems_telemetry_feed PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
add_test(NAME ems_telemetry_feed COMMAND test_ems_telemetry_feed)

add_executable(test_ems_time_series_factory
    test_time_series_factory.cpp ${CMAKE_SOURCE_DIR}/src/scd/time_series_factory.cpp)
target_include_directories(test_ems_time_series_factory PRIVATE ${CMAKE_SOURCE_DIR}/src/scd)
target_link_libraries(test_ems_time_series_factory PRIVATE ems_common_core)
set_target_properties(test_ems_time_series_factory PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
add_test(NAME ems_time_series_factory COMMAND test_ems_time_series_factory)
