file(GLOB_RECURSE RTDB_SOURCES
    *.cpp
)

set(RTDB_SOURCES
    ${RTDB_SOURCES}
)

add_library(rtdb STATIC ${RTDB_SOURCES})

target_include_directories(rtdb PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HIREDIS REQUIRED hiredis)

target_include_directories(rtdb PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${HIREDIS_INCLUDE_DIRS}
)

target_link_libraries(rtdb PUBLIC
    ${HIREDIS_LIBRARIES}
    redis++
    pthread
)

set_target_properties(rtdb PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    POSITION_INDEPENDENT_CODE ON
)
