add_executable(rtdb_example
    example.cpp
)

target_link_libraries(rtdb_example
    rtdb
    ${HIREDIS_LIBRARIES}
    pthread
)

target_include_directories(rtdb_example PRIVATE
    ${CMAKE_SOURCE_DIR}/src/rtdb
)
