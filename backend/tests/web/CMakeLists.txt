include(GoogleTest)
find_package(SQLite3 REQUIRED)

file(GLOB_RECURSE web_test_sources CONFIGURE_DEPENDS "test_*.cpp")
add_executable(test_web ${web_test_sources})
target_link_libraries(test_web PRIVATE ems_web_app ems_database_runtime gtest_main SQLite::SQLite3)
target_include_directories(test_web SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/third_party/ormpp")
target_compile_definitions(
    test_web PRIVATE ORMPP_ENABLE_SQLITE3
                     EMS_WEB_SCHEMA_FILE="${CMAKE_CURRENT_SOURCE_DIR}/schema.sql"
)
set_target_properties(test_web PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
if(MSVC)
    target_compile_options(test_web PRIVATE /Z7)
    target_link_options(test_web PRIVATE /DEBUG:FULL)
endif()
ems_copy_mysql_runtime(test_web)
ems_copy_web_runtime(test_web)
gtest_discover_tests(test_web)
