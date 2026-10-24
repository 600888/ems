cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ems_rtdb VERSION 1.0.0 LANGUAGES C CXX)
endif()
option(EMS_REALTIME_WITH_SHM "Build the Boost.Interprocess backend" ON)
option(EMS_REALTIME_WITH_REDIS "Build the redis-plus-plus backend" ON)
option(EMS_REALTIME_BUILD_PROCESS "Build the ems-rtdb lifecycle entry" ON)
get_filename_component(EMS_RTDB_BACKEND_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(EMS_RTDB_THIRD_PARTY "${EMS_RTDB_BACKEND_DIR}/third_party")
find_package(Threads REQUIRED)
if(NOT TARGET ems_realtime_api)
    add_library(ems_realtime_api INTERFACE)
    target_include_directories(ems_realtime_api INTERFACE "${EMS_RTDB_BACKEND_DIR}/include")
    target_compile_features(ems_realtime_api INTERFACE cxx_std_17)
endif()
add_library(ems_rtdb SHARED store/realtime_store.cpp store/store_detail.cpp
    client/realtime_client.cpp client/rtdb.cpp client/rtdb_manager.cpp client/rtdb_ems.cpp
    model/rtdb_cache.cpp model/rtdb_point.cpp)
add_library(ems::rtdb ALIAS ems_rtdb)
add_library(rtdb ALIAS ems_rtdb)
target_include_directories(
    ems_rtdb
    PUBLIC $<BUILD_INTERFACE:${EMS_RTDB_BACKEND_DIR}/include>
           $<BUILD_INTERFACE:${EMS_RTDB_BACKEND_DIR}/src>
           $<BUILD_INTERFACE:${EMS_RTDB_THIRD_PARTY}/libmodbuspp/3rdparty/json/single_include>
           $<INSTALL_INTERFACE:include>
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/store"
)
target_link_libraries(ems_rtdb PRIVATE Threads::Threads)
target_compile_features(ems_rtdb PUBLIC cxx_std_17)
target_compile_definitions(ems_rtdb PRIVATE EMS_RTDB_BUILD)
set_target_properties(
    ems_rtdb PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO
                        VERSION 1.0.0 SOVERSION 1 EXPORT_NAME rtdb
)
if(MSVC)
    target_compile_options(ems_rtdb PUBLIC /utf-8)
endif()
if(WIN32)
    target_compile_definitions(ems_rtdb PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(ems_rtdb PRIVATE bcrypt)
endif()
if(EMS_REALTIME_WITH_SHM)
    # Use one installed Boost distribution; support header-only roots as well as packages.
    cmake_policy(PUSH)
    if(POLICY CMP0144)
        cmake_policy(SET CMP0144 NEW)
    endif()
    find_package(Boost 1.83 QUIET CONFIG)
    if(NOT Boost_FOUND)
        if(POLICY CMP0167)
            cmake_policy(SET CMP0167 OLD)
        endif()
        find_package(Boost 1.83 REQUIRED MODULE)
    endif()
    cmake_policy(POP)
    target_sources(ems_rtdb PRIVATE backend/shared_memory_store.cpp)
    target_compile_definitions(ems_rtdb PRIVATE EMS_REALTIME_WITH_SHM)
    target_link_libraries(ems_rtdb PRIVATE Boost::headers)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(ems_rtdb PRIVATE rt)
    elseif(WIN32)
        target_link_libraries(ems_rtdb PRIVATE ole32 oleaut32 psapi advapi32)
    endif()
endif()
if(EMS_REALTIME_WITH_REDIS)
    if(NOT TARGET redis++)
        set(REDIS_PLUS_PLUS_BUILD_TEST OFF CACHE BOOL "Disable upstream Redis tests" FORCE)
        set(REDIS_PLUS_PLUS_BUILD_STATIC OFF CACHE BOOL "Use the Redis shared library" FORCE)
        set(REDIS_PLUS_PLUS_BUILD_SHARED ON CACHE BOOL "Build the Redis shared library" FORCE)
        add_subdirectory("${EMS_RTDB_THIRD_PARTY}/redis-plus-plus"
                         "${CMAKE_CURRENT_BINARY_DIR}/redis-plus-plus")
    endif()
    target_sources(ems_rtdb PRIVATE backend/redis_store.cpp)
    target_compile_definitions(ems_rtdb PRIVATE EMS_REALTIME_WITH_REDIS)
    target_link_libraries(ems_rtdb PRIVATE redis++)
    if(WIN32)
        add_custom_command(TARGET ems_rtdb POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:ems_rtdb> $<TARGET_FILE_DIR:ems_rtdb> COMMAND_EXPAND_LISTS)
    endif()
endif()
# 旧目标兼容入口，全部链接同一个动态库。
foreach(compat IN ITEMS ems_realtime_core ems_realtime_shm ems_realtime_redis ems_realtime_client)
    add_library(${compat} INTERFACE)
    target_link_libraries(${compat} INTERFACE ems_rtdb)
endforeach()
if(EMS_REALTIME_BUILD_PROCESS)
    add_executable(ems-rtdb process/main.cpp)
    target_link_libraries(ems-rtdb PRIVATE ems_rtdb)
    set_target_properties(ems-rtdb PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES
                                             CXX_EXTENSIONS NO)
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(EMS_RTDB_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/ems_rtdb")
configure_package_config_file("${EMS_RTDB_BACKEND_DIR}/cmake/ems_rtdbConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ems_rtdbConfig.cmake"
    INSTALL_DESTINATION "${EMS_RTDB_CMAKE_INSTALL_DIR}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/ems_rtdbConfigVersion.cmake"
    VERSION 1.0.0 COMPATIBILITY SameMajorVersion)
install(TARGETS ems_rtdb EXPORT ems_rtdbTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT EMSRTDB
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT EMSRTDB
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT EMSRTDB)
if(TARGET ems-rtdb)
    install(TARGETS ems-rtdb RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT EMSRTDB)
endif()
if(WIN32 AND EMS_REALTIME_WITH_REDIS)
    install(FILES $<TARGET_RUNTIME_DLLS:ems_rtdb> DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT EMSRTDB)
elseif(EMS_REALTIME_WITH_REDIS)
    install(TARGETS redis++ LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT EMSRTDB)
endif()
install(DIRECTORY "${EMS_RTDB_BACKEND_DIR}/include/rtdb"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT EMSRTDB)
install(FILES "${EMS_RTDB_BACKEND_DIR}/src/models/base.h"
              "${EMS_RTDB_BACKEND_DIR}/src/models/point_runtime.hpp"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/models" COMPONENT EMSRTDB)
install(FILES "${EMS_RTDB_BACKEND_DIR}/include/net/protocol/modbus/modbus_types.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/net/protocol/modbus" COMPONENT EMSRTDB)
install(FILES "${EMS_RTDB_BACKEND_DIR}/include/net/protocol/enum_value.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/net/protocol" COMPONENT EMSRTDB)
install(DIRECTORY "${EMS_RTDB_THIRD_PARTY}/libmodbuspp/3rdparty/json/single_include/nlohmann"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT EMSRTDB)
install(DIRECTORY "${EMS_RTDB_BACKEND_DIR}/include/common/core"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/common" COMPONENT EMSRTDB)
install(FILES "${EMS_RTDB_BACKEND_DIR}/conf/realtime.json"
    "${EMS_RTDB_BACKEND_DIR}/conf/realtime-redis.json"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/ems_rtdb" COMPONENT EMSRTDB)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/ems_rtdb" COMPONENT EMSRTDB)
if(EMS_REALTIME_WITH_SHM)
    find_file(EMS_RTDB_BOOST_LICENSE_FILE LICENSE_1_0.txt HINTS ${Boost_INCLUDE_DIRS}
              PATH_SUFFIXES ..)
    if(EMS_RTDB_BOOST_LICENSE_FILE)
        install(FILES "${EMS_RTDB_BOOST_LICENSE_FILE}"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/ems_rtdb/licenses"
                RENAME boost.txt COMPONENT EMSRTDB)
    endif()
endif()
if(EMS_REALTIME_WITH_REDIS)
    install(FILES "${EMS_RTDB_THIRD_PARTY}/redis-plus-plus/LICENSE"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/ems_rtdb/licenses"
            RENAME redis-plus-plus.txt COMPONENT EMSRTDB)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/licenses/hiredis.txt"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/ems_rtdb/licenses" COMPONENT EMSRTDB)
endif()
install(FILES "${EMS_RTDB_THIRD_PARTY}/libmodbuspp/3rdparty/json/LICENSE.MIT"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/ems_rtdb/licenses"
        RENAME nlohmann-json.txt COMPONENT EMSRTDB)
install(EXPORT ems_rtdbTargets NAMESPACE ems:: DESTINATION "${EMS_RTDB_CMAKE_INSTALL_DIR}"
        COMPONENT EMSRTDB)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ems_rtdbConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ems_rtdbConfigVersion.cmake"
    DESTINATION "${EMS_RTDB_CMAKE_INSTALL_DIR}" COMPONENT EMSRTDB)
