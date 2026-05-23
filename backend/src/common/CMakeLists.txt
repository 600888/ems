# common/db - EMS 数据库操作动态库
# 封装 ormpp 数据库操作，编译为 libems_db.so

add_library(ems_db SHARED
    db/db_manager.cpp
)

target_include_directories(ems_db
PUBLIC
    ${CMAKE_SOURCE_DIR}/inc
    ${CMAKE_SOURCE_DIR}/3rd
    ${CMAKE_SOURCE_DIR}/3rd/ormpp/ormpp
    ${CMAKE_SOURCE_DIR}/3rd/ormpp
    ${CMAKE_SOURCE_DIR}/3rd/libmodbuspp/3rdparty/json/single_include
)

target_link_libraries(ems_db
PRIVATE
    Threads::Threads
)

if(MYSQL_FOUND)
    target_link_libraries(ems_db PRIVATE ${MYSQL_LIBRARIES})
    target_include_directories(ems_db PRIVATE ${MYSQL_INCLUDE_DIRS})
    target_compile_definitions(ems_db PRIVATE EMS_DB_HAS_MYSQL)
    message(STATUS "ems_db: MySQL support enabled")
else()
    message(STATUS "ems_db: MySQL support disabled (libmysqlclient not found)")
endif()

# Optional SQLite3
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SQLITE3 sqlite3)
    if(SQLITE3_FOUND)
        target_link_libraries(ems_db PRIVATE ${SQLITE3_LIBRARIES})
        target_include_directories(ems_db PRIVATE ${SQLITE3_INCLUDE_DIRS})
        message(STATUS "ems_db: SQLite support enabled")
    endif()
endif()

target_compile_definitions(ems_db PRIVATE EMS_DB_BUILD)

set_target_properties(ems_db PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

message(STATUS "EMS DB library configured (shared: libems_db.so)")
