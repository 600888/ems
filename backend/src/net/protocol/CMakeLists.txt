# 通讯协议子工程可独立构建，不依赖 MySQL/Redis 或其他协议栈。
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.15)
    project(ems_protocols LANGUAGES C CXX)
    option(BUILD_TESTS "Build protocol integration tests" ON)
    enable_testing()
endif()

get_filename_component(EMS_BACKEND_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

find_package(Threads REQUIRED)

if(MSVC)
    add_compile_options(/utf-8)
endif()

add_library(ems_protocol_runtime STATIC ../collect/plugin_manager.cpp platform/dynamic_library.cpp)

target_include_directories(
    ems_protocol_runtime PUBLIC "${EMS_BACKEND_ROOT}/include" "${EMS_BACKEND_ROOT}/src"
)

target_link_libraries(ems_protocol_runtime PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

set_target_properties(
    ems_protocol_runtime
    PROPERTIES CXX_STANDARD 17
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
               POSITION_INDEPENDENT_CODE ON
)

option(EMS_BUILD_MODBUS "Build Modbus protocol package" ON)
option(EMS_BUILD_IEC104 "Build IEC104 protocol package" ON)
option(EMS_BUILD_DLT645 "Build DL/T 645-2007 protocol package" ON)
option(EMS_BUILD_DLT698 "Build DL/T 698.45 protocol package" ON)
set(EMS_PROTOCOL_RUNTIME_DEPENDENCIES
    ""
    CACHE STRING "Runtime dependency files to deploy with protocol modules"
)

if(EMS_BUILD_MODBUS)
    add_subdirectory(modbus)
endif()

if(EMS_BUILD_IEC104)
    add_subdirectory(iec104)
endif()

if(EMS_BUILD_DLT645)
    add_subdirectory(dlt645)
endif()

if(EMS_BUILD_DLT698)
    add_subdirectory(dlt698)
endif()

if(BUILD_TESTS)
    add_subdirectory("${EMS_BACKEND_ROOT}/tests/net/protocol" "${CMAKE_CURRENT_BINARY_DIR}/tests")
endif()

install(TARGETS ems_protocol_runtime ARCHIVE DESTINATION lib)

install(
    FILES "${EMS_BACKEND_ROOT}/include/net/protocol/plugin_manager.h"
          "${EMS_BACKEND_ROOT}/include/net/protocol/dynamic_library.h"
          "${EMS_BACKEND_ROOT}/include/net/protocol/protocol_plugin_api.h"
    DESTINATION include/net/protocol
)
