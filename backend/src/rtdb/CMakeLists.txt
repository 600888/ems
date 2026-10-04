# Redis 实时数据库封装；依赖通过平台无关目标传递。
file(GLOB_RECURSE RTDB_SOURCES CONFIGURE_DEPENDS *.cpp)

add_library(rtdb STATIC ${RTDB_SOURCES})

target_include_directories(
    rtdb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include
                ${CMAKE_SOURCE_DIR}/3rd/libmodbuspp/3rdparty/json/single_include
)

target_link_libraries(rtdb PUBLIC ems_hiredis redis++ Threads::Threads ems_models)

set_target_properties(
    rtdb
    PROPERTIES CXX_STANDARD 20
               CXX_STANDARD_REQUIRED YES
               POSITION_INDEPENDENT_CODE ON
)
