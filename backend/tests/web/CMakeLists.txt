include(GoogleTest)
if(NOT TARGET SQLite::SQLite3)
    find_package(SQLite3 REQUIRED)
endif()

file(GLOB_RECURSE web_test_sources CONFIGURE_DEPENDS "test_*.cpp")
list(REMOVE_ITEM web_test_sources "${CMAKE_CURRENT_SOURCE_DIR}/test_identity_mysql.cpp")
add_executable(test_web ${web_test_sources})
target_link_libraries(test_web PRIVATE ems_web_app ems_database_runtime gtest_main SQLite::SQLite3)
target_include_directories(test_web SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/third_party/ormpp")
target_compile_definitions(
    test_web PRIVATE ORMPP_ENABLE_SQLITE3
                     EMS_WEB_SCHEMA_FILE="${CMAKE_CURRENT_SOURCE_DIR}/schema.sql"
                     EMS_IDENTITY_SCHEMA_FILE="${CMAKE_CURRENT_SOURCE_DIR}/identity_schema.sql"
)
set_target_properties(test_web PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
if(MSVC)
    target_compile_options(test_web PRIVATE /Z7)
    target_link_options(test_web PRIVATE /DEBUG:FULL)
endif()
ems_copy_mysql_runtime(test_web)
ems_copy_web_runtime(test_web)
ems_copy_directory_runtime_dependencies()

gtest_discover_tests(test_web)

# ormpp 的 SQL 方言由宏选择，真实 MySQL 用独立二进制，避免与 SQLite 实例化发生 ODR 冲突。
add_executable(test_web_mysql test_identity_mysql.cpp)
target_link_libraries(test_web_mysql PRIVATE ems_web_app gtest_main)
set_target_properties(test_web_mysql PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
ems_copy_mysql_runtime(test_web_mysql)
ems_copy_web_runtime(test_web_mysql)
gtest_discover_tests(test_web_mysql)
