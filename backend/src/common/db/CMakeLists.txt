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

# 与 Web 依赖一样，复用准备脚本安装的同工具链客户端。
set(EMS_DATABASE_DEPENDENCY_PREFIX
    ""
    CACHE PATH "Optional installation prefix for the MySQL/MariaDB client"
)
if(EMS_DATABASE_DEPENDENCY_PREFIX)
    list(PREPEND CMAKE_PREFIX_PATH "${EMS_DATABASE_DEPENDENCY_PREFIX}")
elseif(WIN32 AND (MSVC OR MINGW))
    if(MSVC)
        set(_ems_database_toolchain msvc)
    else()
        set(_ems_database_toolchain mingw)
    endif()
    list(APPEND CMAKE_PREFIX_PATH
         "${CMAKE_SOURCE_DIR}/build/database-deps/mariadb-${_ems_database_toolchain}/install"
    )
endif()
if(EMS_DATABASE_MYSQL_INCLUDE_DIR AND NOT EXISTS "${EMS_DATABASE_MYSQL_INCLUDE_DIR}/mysql.h")
    unset(EMS_DATABASE_MYSQL_INCLUDE_DIR CACHE)
    unset(EMS_DATABASE_MYSQL_INCLUDE_DIR)
endif()
if(EMS_DATABASE_MYSQL_LIBRARY AND NOT EXISTS "${EMS_DATABASE_MYSQL_LIBRARY}")
    unset(EMS_DATABASE_MYSQL_LIBRARY CACHE)
    unset(EMS_DATABASE_MYSQL_LIBRARY)
endif()

find_path(
    EMS_DATABASE_MYSQL_INCLUDE_DIR
    NAMES mysql.h
    PATH_SUFFIXES mysql mariadb
)
find_library(EMS_DATABASE_MYSQL_LIBRARY NAMES mysqlclient mariadb libmariadb
             PATH_SUFFIXES mysql mariadb
)
if(EMS_DATABASE_MYSQL_INCLUDE_DIR AND EMS_DATABASE_MYSQL_LIBRARY)
    if(WIN32)
        get_filename_component(_ems_mysql_lib_dir "${EMS_DATABASE_MYSQL_LIBRARY}" DIRECTORY)
        find_file(
            EMS_DATABASE_MYSQL_RUNTIME
            NAMES libmariadb.dll libmysql.dll
            HINTS "${_ems_mysql_lib_dir}" "${_ems_mysql_lib_dir}/../bin"
                  "${_ems_mysql_lib_dir}/../../bin"
        )
        find_path(
            EMS_DATABASE_MYSQL_PLUGIN_DIR
            NAMES caching_sha2_password.dll sha256_password.dll
            HINTS "${_ems_mysql_lib_dir}/plugin" "${_ems_mysql_lib_dir}/../lib/plugin"
                  "${_ems_mysql_lib_dir}/mariadb/plugin" "${_ems_mysql_lib_dir}/../lib/mariadb/plugin"
                  "${_ems_mysql_lib_dir}/../lib64/mysql/plugin" "${_ems_mysql_lib_dir}/../bin"
                  "${_ems_mysql_lib_dir}/.."
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
    if(EMS_BUILD_WEB OR EMS_BUILD_COLLECTOR)
        message(FATAL_ERROR
            "MySQL/MariaDB client headers and library are required by ems-web/ems-collector. "
            "On Windows, run scripts/setup_database_dependencies.ps1 -Toolchain msvc (or mingw) "
            "from the repository root in the matching compiler terminal, then configure again. "
            "For an external installation, set EMS_DATABASE_DEPENDENCY_PREFIX or CMAKE_PREFIX_PATH, "
            "or set EMS_DATABASE_MYSQL_INCLUDE_DIR and EMS_DATABASE_MYSQL_LIBRARY explicitly."
        )
    endif()
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
        ems_copy_runtime_files(${target} "${EMS_DATABASE_MYSQL_RUNTIME}")
        foreach(_ems_auth IN ITEMS caching_sha2_password sha256_password)
            if(EXISTS "${EMS_DATABASE_MYSQL_PLUGIN_DIR}/${_ems_auth}.dll")
                ems_copy_runtime_files(${target} "${EMS_DATABASE_MYSQL_PLUGIN_DIR}/${_ems_auth}.dll")
            endif()
        endforeach()
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
