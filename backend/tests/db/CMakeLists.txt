include(GoogleTest)
if(NOT TARGET SQLite::SQLite3)
    find_package(SQLite3 QUIET)
endif()

add_executable(test_point_table_db test_point_table_db.cpp)
target_link_libraries(
    test_point_table_db PRIVATE ems_database_configuration ems_database_runtime ems_excel
                                net_para_loader gtest_main
)
target_compile_definitions(
    test_point_table_db PRIVATE EMS_POINT_DATA_DIR="${CMAKE_SOURCE_DIR}/../data"
)
set_target_properties(test_point_table_db PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
if(TARGET SQLite::SQLite3)
    target_link_libraries(test_point_table_db PRIVATE SQLite::SQLite3)
    target_compile_definitions(test_point_table_db PRIVATE EMS_TEST_SQLITE ORMPP_ENABLE_SQLITE3)
    target_include_directories(
        test_point_table_db SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/ormpp
    )

endif()
target_include_directories(test_point_table_db PRIVATE ${CMAKE_SOURCE_DIR}/src)
ems_copy_directory_runtime_dependencies()

gtest_discover_tests(test_point_table_db)
