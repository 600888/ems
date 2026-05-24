if(NOT BUILD_TESTS)
    return()
endif()

include(GoogleTest)

add_executable(test_modbus_handler
    modbus_handler_test.cpp
    ${CMAKE_SOURCE_DIR}/src/net/protocol/handler/base_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/net/protocol/modbus/modbus_handler.cpp
)

target_include_directories(test_modbus_handler PRIVATE
    ${CMAKE_SOURCE_DIR}/inc
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/3rd/libmodbuspp/lib/include
    ${CMAKE_SOURCE_DIR}/3rd/googletest/googletest/include
    ${CMAKE_SOURCE_DIR}/3rd/googletest/googlemock/include
)

target_link_libraries(test_modbus_handler PRIVATE
    gtest
    gmock
    pthread
    modbus_protocol
    ${LIBMODBUS_LIBRARIES}
)

gtest_discover_tests(test_modbus_handler)

message(STATUS "Created test_modbus_handler executable")
