get_target_property(_ems_mysql_available ems_database_mysql EMS_MYSQL_AVAILABLE)
if(_ems_mysql_available)
    add_executable(ems-strategy main.cpp)
    target_include_directories(
        ems-strategy PRIVATE ${CMAKE_SOURCE_DIR}/src
                             ${CMAKE_SOURCE_DIR}/third_party
                             ${CMAKE_SOURCE_DIR}/third_party/libmodbuspp/3rdparty/json/single_include
    )
    target_link_libraries(ems-strategy PRIVATE ems_database_mysql ems::rtdb ems_common_codec Threads::Threads)
    set_target_properties(ems-strategy PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
    ems_copy_mysql_runtime(ems-strategy)
    ems_copy_directory_runtime_dependencies()
endif()
