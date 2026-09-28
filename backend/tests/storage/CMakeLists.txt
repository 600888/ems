if(NOT BUILD_TESTS)
    return()
endif()

add_executable(test_ems_storage test_storage.cpp)
target_link_libraries(test_ems_storage PRIVATE ems_storage_core ems_common_codec)
set_target_properties(test_ems_storage PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
add_test(NAME ems_storage COMMAND test_ems_storage)
