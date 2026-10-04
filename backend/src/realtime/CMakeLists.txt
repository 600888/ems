# 实时存储核心及可选共享内存 / Redis 后端，支持独立构建。

option(EMS_REALTIME_WITH_SHM "Build the shared-memory realtime backend" ON)
option(EMS_REALTIME_WITH_REDIS "Build the Redis realtime backend" ON)
set(EMS_HIREDIS_SOURCE_DIR
    ""
    CACHE PATH "Optional local hiredis >= 1.0 source checkout (no automatic download)"
)

find_package(Threads REQUIRED)

if(NOT TARGET ems_realtime_api)
    add_library(ems_realtime_api INTERFACE)

    target_include_directories(ems_realtime_api INTERFACE ${CMAKE_CURRENT_LIST_DIR}/../../include)

    target_compile_features(ems_realtime_api INTERFACE cxx_std_17)
endif()

add_library(ems_realtime_core STATIC realtime_store.cpp store_detail.cpp)

target_link_libraries(
    ems_realtime_core
    PUBLIC ems_realtime_api
    PRIVATE Threads::Threads
)

if(WIN32)
    target_link_libraries(ems_realtime_core PRIVATE bcrypt)
endif()

set_target_properties(
    ems_realtime_core
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
)

if(EMS_REALTIME_WITH_SHM)
    if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "Shared memory currently supports Windows and Linux only")
    endif()

    add_library(ems_realtime_shm STATIC shared_memory_store.cpp)

    target_link_libraries(
        ems_realtime_shm
        PUBLIC ems_realtime_core
        PRIVATE Threads::Threads
    )

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(ems_realtime_shm PRIVATE rt)
    endif()

    set_target_properties(
        ems_realtime_shm
        PROPERTIES CXX_STANDARD 17
                   CXX_STANDARD_REQUIRED YES
                   CXX_EXTENSIONS NO
    )
endif()

if(EMS_REALTIME_WITH_REDIS)
    # 完整后端复用统一依赖；独立测试仍可自行查找或构建 hiredis。
    if(EMS_HIREDIS_SOURCE_DIR AND NOT TARGET hiredis::hiredis)
        enable_language(C)
        set(DISABLE_TESTS
            ON
            CACHE BOOL "Disable hiredis tests" FORCE
        )
        set(ENABLE_EXAMPLES
            OFF
            CACHE BOOL "Disable hiredis examples" FORCE
        )
        set(BUILD_SHARED_LIBS OFF)
        set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

        add_subdirectory(
            ${EMS_HIREDIS_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/hiredis EXCLUDE_FROM_ALL
        )
    endif()

    if(NOT TARGET hiredis::hiredis)
        find_package(hiredis 1.0 CONFIG QUIET)
    endif()

    if(TARGET ems_hiredis)
        set(EMS_HIREDIS_TARGET ems_hiredis)
    elseif(NOT TARGET hiredis::hiredis)
        find_path(EMS_HIREDIS_INCLUDE_DIR hiredis.h PATH_SUFFIXES hiredis REQUIRED)

        find_library(EMS_HIREDIS_LIBRARY NAMES hiredis REQUIRED)

        add_library(ems_hiredis UNKNOWN IMPORTED)

        set_target_properties(
            ems_hiredis PROPERTIES IMPORTED_LOCATION "${EMS_HIREDIS_LIBRARY}"
                                   INTERFACE_INCLUDE_DIRECTORIES "${EMS_HIREDIS_INCLUDE_DIR}"
        )
        set(EMS_HIREDIS_TARGET ems_hiredis)
    else()
        set(EMS_HIREDIS_TARGET hiredis::hiredis)
    endif()

    add_library(ems_realtime_redis STATIC redis_store.cpp)

    target_link_libraries(
        ems_realtime_redis
        PUBLIC ems_realtime_core
        PRIVATE ${EMS_HIREDIS_TARGET} Threads::Threads
    )

    if(WIN32)
        target_link_libraries(ems_realtime_redis PRIVATE ws2_32)
    endif()

    set_target_properties(
        ems_realtime_redis
        PROPERTIES CXX_STANDARD 17
                   CXX_STANDARD_REQUIRED YES
                   CXX_EXTENSIONS NO
    )
endif()
