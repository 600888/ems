# Modbus Protocol Implementation
# Supports Modbus RTU (Serial) and Modbus TCP (Ethernet) communications

add_library(modbus_protocol STATIC
    modbus_client.cpp
    modbus_server.cpp
)

target_include_directories(modbus_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/3rd/libmodbuspp/include
    ${CMAKE_SOURCE_DIR}/inc
)

target_link_libraries(modbus_protocol PUBLIC
    ${LIBMODBUS_LIBRARIES}
    modbuspp-shared
    Threads::Threads
)
