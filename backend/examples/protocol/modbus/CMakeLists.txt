# Modbus Protocol Examples
# Examples demonstrating Modbus client and server functionality

set(EXAMPLE_SOURCES
    tcp_client.cpp
    rtu_client.cpp
    tcp_server.cpp
    rtu_server.cpp
)

foreach(src ${EXAMPLE_SOURCES})
    get_filename_component(target ${src} NAME_WE)
    add_executable(${target} ${src})
    target_include_directories(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/net/protocol/modbus
        ${CMAKE_SOURCE_DIR}/3rd/libmodbuspp/include
        ${CMAKE_SOURCE_DIR}/inc
    )
    target_link_libraries(${target} PRIVATE
        modbuspp
        modbus
        modbus_protocol
        Threads::Threads
    )
endforeach()

message(STATUS "Modbus examples: ${EXAMPLE_SOURCES}")
