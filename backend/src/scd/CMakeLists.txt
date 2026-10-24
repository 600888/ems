# 存储核心不依赖外部服务；原生进程按 MySQL / ZeroMQ 依赖启用。

add_library(
    ems_storage_core STATIC storage.cpp storage.h telemetry_feed.h outbox_publisher.cpp
                            outbox_publisher.h
)

target_include_directories(ems_storage_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

target_include_directories(
    ems_storage_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                             ${CMAKE_SOURCE_DIR}/third_party/libmodbuspp/3rdparty/json/single_include
)

target_link_libraries(
    ems_storage_core
    PUBLIC ems_common_core ems_messaging_api ems_database_api ems_timeseries_api
    PRIVATE ems_common_codec
)

set_target_properties(
    ems_storage_core
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
)

option(EMS_STORAGE_WITH_NATIVE "Build ems-storage with MySQL and ZeroMQ" ON)

if(EMS_STORAGE_WITH_NATIVE AND TARGET ems_zmq_messaging)
    get_target_property(_ems_mysql_available ems_database_mysql EMS_MYSQL_AVAILABLE)
    if(_ems_mysql_available)

        add_library(
            ems_storage_native STATIC zmq_telemetry.cpp native_adapters.h
        )

        target_include_directories(
            ems_storage_native
            PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
        )

        target_link_libraries(
            ems_storage_native
            PUBLIC ems_storage_core
            PRIVATE ems_common_codec ems_zmq_messaging ems_database_mysql ems_timeseries
                    Threads::Threads
        )

        if(WIN32)
            target_link_libraries(ems_storage_native PRIVATE ws2_32)
        endif()

        set_target_properties(
            ems_storage_native
            PROPERTIES CXX_STANDARD 17
                       CXX_STANDARD_REQUIRED YES
                       CXX_EXTENSIONS NO
        )

        add_executable(ems-storage main.cpp)
        ems_copy_mysql_runtime(ems-storage)

        target_link_libraries(ems-storage PRIVATE ems_storage_native)

        set_target_properties(
            ems-storage
            PROPERTIES CXX_STANDARD 17
                       CXX_STANDARD_REQUIRED YES
                       CXX_EXTENSIONS NO
        )
    else()
        message(STATUS "ems-storage native process disabled: MySQL client missing")
    endif()
elseif(EMS_STORAGE_WITH_NATIVE)
    message(STATUS "ems-storage native process disabled: configure with EMS_WITH_ZEROMQ=ON")
endif()
