add_library(ems_storage_core STATIC storage.cpp outbox_publisher.cpp)
target_include_directories(ems_storage_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_include_directories(ems_storage_core PRIVATE
    ${CMAKE_SOURCE_DIR}/3rd/libmodbuspp/3rdparty/json/single_include)
target_link_libraries(ems_storage_core PUBLIC ems_common_core ems_messaging_api
                      PRIVATE ems_common_codec)
set_target_properties(ems_storage_core PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)

option(EMS_STORAGE_WITH_NATIVE "Build ems-storage with MySQL, TDengine and hiredis" ON)
if(EMS_STORAGE_WITH_NATIVE)
    find_path(EMS_STORAGE_MYSQL_INCLUDE_DIR mysql/mysql.h)
    find_path(EMS_STORAGE_MYSQL_HEADER_DIR mysql.h PATH_SUFFIXES mysql)
    find_library(EMS_STORAGE_MYSQL_LIBRARY NAMES mysqlclient mariadb)
    find_path(EMS_STORAGE_TAOS_INCLUDE_DIR taos.h)
    find_library(EMS_STORAGE_TAOS_LIBRARY NAMES taos)
    find_path(EMS_STORAGE_HIREDIS_INCLUDE_DIR hiredis.h PATH_SUFFIXES hiredis)
    find_library(EMS_STORAGE_HIREDIS_LIBRARY NAMES hiredis)
    if(EMS_STORAGE_MYSQL_INCLUDE_DIR AND EMS_STORAGE_MYSQL_HEADER_DIR AND
       EMS_STORAGE_MYSQL_LIBRARY AND
       EMS_STORAGE_TAOS_INCLUDE_DIR AND EMS_STORAGE_TAOS_LIBRARY AND
       EMS_STORAGE_HIREDIS_INCLUDE_DIR AND EMS_STORAGE_HIREDIS_LIBRARY)
        add_library(ems_storage_native STATIC
            mysql_history.cpp taos_series.cpp redis_stream.cpp)
        target_include_directories(ems_storage_native PRIVATE
            ${EMS_STORAGE_MYSQL_INCLUDE_DIR}
            ${EMS_STORAGE_MYSQL_HEADER_DIR}
            ${CMAKE_SOURCE_DIR}/3rd/ormpp
            ${EMS_STORAGE_TAOS_INCLUDE_DIR}
            ${EMS_STORAGE_HIREDIS_INCLUDE_DIR})
        target_compile_definitions(ems_storage_native PRIVATE ORMPP_ENABLE_MYSQL)
        target_link_libraries(ems_storage_native PUBLIC ems_storage_core
            PRIVATE ems_common_codec
                    ${EMS_STORAGE_MYSQL_LIBRARY}
                    ${EMS_STORAGE_TAOS_LIBRARY}
                    ${EMS_STORAGE_HIREDIS_LIBRARY}
                    Threads::Threads)
        if(WIN32)
            target_link_libraries(ems_storage_native PRIVATE ws2_32)
        endif()
        set_target_properties(ems_storage_native PROPERTIES
            CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
        add_executable(ems-storage main.cpp)
        target_link_libraries(ems-storage PRIVATE ems_storage_native)
        set_target_properties(ems-storage PROPERTIES
            CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
    else()
        message(STATUS "ems-storage native process disabled: MySQL, TDengine or hiredis client missing")
    endif()
endif()
