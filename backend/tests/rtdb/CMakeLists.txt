# Redis 实时数据库使用示例。

add_executable(rtdb_example example.cpp)

target_link_libraries(rtdb_example PRIVATE rtdb Threads::Threads)

target_include_directories(rtdb_example PRIVATE ${CMAKE_SOURCE_DIR}/src/rtdb)
