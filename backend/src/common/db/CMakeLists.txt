file(GLOB_RECURSE ems_configuration_sources CONFIGURE_DEPENDS "configuration/*.cpp")
add_library(ems_database_configuration STATIC ${ems_configuration_sources})
target_link_libraries(
    ems_database_configuration
    PUBLIC ems_database_api
    PRIVATE ems_excel
)
target_include_directories(
    ems_database_configuration SYSTEM
    PRIVATE ${CMAKE_SOURCE_DIR}/third_party/libmodbuspp/3rdparty/json/single_include
)
set_target_properties(
    ems_database_configuration
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
               POSITION_INDEPENDENT_CODE ON
)

add_library(ems_database_runtime STATIC runtime/transaction_database.cpp)
target_include_directories(ems_database_runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    ems_database_runtime
    PUBLIC ems_database_api
    PRIVATE Threads::Threads
)
set_target_properties(
    ems_database_runtime
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
               POSITION_INDEPENDENT_CODE ON
)

find_path(
    EMS_DATABASE_MYSQL_INCLUDE_DIR
    NAMES mysql.h
    PATH_SUFFIXES mysql mariadb
)
find_library(EMS_DATABASE_MYSQL_LIBRARY NAMES mysqlclient mariadb libmariadb)
if(EMS_DATABASE_MYSQL_INCLUDE_DIR AND EMS_DATABASE_MYSQL_LIBRARY)
    if(WIN32)
        get_filename_component(_ems_mysql_lib_dir "${EMS_DATABASE_MYSQL_LIBRARY}" DIRECTORY)
        find_file(
            EMS_DATABASE_MYSQL_RUNTIME
            NAMES libmariadb.dll libmysql.dll
            HINTS "${_ems_mysql_lib_dir}" "${_ems_mysql_lib_dir}/../bin"
                  "${_ems_mysql_lib_dir}/../../bin"
        )
    endif()
    add_library(ems_database_mysql STATIC mysql/mysql_database.cpp)
    target_include_directories(
        ems_database_mysql SYSTEM PRIVATE ${EMS_DATABASE_MYSQL_INCLUDE_DIR}
                                          ${CMAKE_SOURCE_DIR}/third_party/ormpp
    )
    target_compile_definitions(ems_database_mysql PRIVATE ORMPP_ENABLE_MYSQL)
    target_link_libraries(ems_database_mysql PRIVATE ${EMS_DATABASE_MYSQL_LIBRARY})
    set_property(TARGET ems_database_mysql PROPERTY EMS_MYSQL_AVAILABLE TRUE)
    if(WIN32)
        target_link_libraries(ems_database_mysql PRIVATE ws2_32)
    endif()
else()
    add_library(ems_database_mysql STATIC mysql/mysql_unavailable.cpp)
    set_property(TARGET ems_database_mysql PROPERTY EMS_MYSQL_AVAILABLE FALSE)
endif()
target_link_libraries(
    ems_database_mysql
    PUBLIC ems_database_api ems_database_configuration
    PRIVATE ems_database_runtime
)
set_target_properties(
    ems_database_mysql
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
               POSITION_INDEPENDENT_CODE ON
)

function(ems_copy_mysql_runtime target)
    if(WIN32 AND EMS_DATABASE_MYSQL_RUNTIME)
        add_custom_command(
            TARGET ${target}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EMS_DATABASE_MYSQL_RUNTIME}"
                    "$<TARGET_FILE_DIR:${target}>"
            VERBATIM
        )
    endif()
endfunction()

add_library(ems_timeseries_api INTERFACE)
target_link_libraries(ems_timeseries_api INTERFACE ems_database_api)
add_library(ems_timeseries STATIC timeseries/factory.cpp)
target_link_libraries(ems_timeseries PUBLIC ems_timeseries_api)
set_target_properties(
    ems_timeseries
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
               POSITION_INDEPENDENT_CODE ON
)

find_path(EMS_DATABASE_TAOS_INCLUDE_DIR taos.h)
find_library(EMS_DATABASE_TAOS_LIBRARY NAMES taos)
if(EMS_DATABASE_TAOS_INCLUDE_DIR AND EMS_DATABASE_TAOS_LIBRARY)
    add_library(ems_timeseries_taos STATIC timeseries/taos/taos_series.cpp)
    target_include_directories(ems_timeseries_taos PRIVATE ${EMS_DATABASE_TAOS_INCLUDE_DIR})
    target_link_libraries(
        ems_timeseries_taos
        PUBLIC ems_timeseries_api
        PRIVATE ${EMS_DATABASE_TAOS_LIBRARY} Threads::Threads
    )
    set_target_properties(
        ems_timeseries_taos
        PROPERTIES CXX_STANDARD 17
                   CXX_STANDARD_REQUIRED YES
                   CXX_EXTENSIONS NO
                   POSITION_INDEPENDENT_CODE ON
    )
    target_compile_definitions(ems_timeseries PRIVATE EMS_DATABASE_HAVE_TAOS)
    target_link_libraries(ems_timeseries PRIVATE ems_timeseries_taos)
endif()
