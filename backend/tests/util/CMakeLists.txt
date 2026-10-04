include(GoogleTest)

add_executable(test_excel test_excel.cpp)
target_link_libraries(test_excel PRIVATE ems_excel gtest_main)
set_target_properties(test_excel PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
gtest_discover_tests(test_excel)
