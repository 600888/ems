# 线程使用 CMake 平台目标：Linux 链接 pthread，Windows 使用原生线程。
find_package(Threads REQUIRED)

# 历史 libmodbuspp 构建脚本依赖 pkg-config；Windows 默认使用独立 C 插件。
if(WIN32)
    set(_ems_legacy_modbus_default OFF)
else()
    set(_ems_legacy_modbus_default ON)
endif()

option(EMS_BUILD_LEGACY_MODBUS "Build legacy libmodbuspp handlers and examples"
       ${_ems_legacy_modbus_default}
)

if(EMS_BUILD_LEGACY_MODBUS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBMODBUS REQUIRED libmodbus>=3.1.6)
    include_directories(${LIBMODBUS_INCLUDE_DIRS})
endif()

# 优先使用 hiredis 的 CMake 包；兼容只有头文件和库的安装目录。
# HIREDIS_HEADER / HIREDIS_LIB 同时供仓库内 redis-plus-plus 的查找脚本使用。
find_package(hiredis 1.0 CONFIG QUIET)
find_path(
    HIREDIS_HEADER
    NAMES hiredis/hiredis.h
    HINTS ${hiredis_INCLUDE_DIRS}
)

if(NOT HIREDIS_HEADER)
    message(FATAL_ERROR "hiredis >= 1.0 headers missing; set CMAKE_PREFIX_PATH or HIREDIS_HEADER")
endif()

add_library(ems_hiredis INTERFACE)
target_include_directories(
    ems_hiredis SYSTEM INTERFACE "${HIREDIS_HEADER}" "${HIREDIS_HEADER}/hiredis"
)

if(TARGET hiredis::hiredis)
    target_link_libraries(ems_hiredis INTERFACE hiredis::hiredis)
else()
    find_library(HIREDIS_LIB NAMES hiredis)

    if(NOT HIREDIS_LIB)
        message(FATAL_ERROR "hiredis library missing; set CMAKE_PREFIX_PATH or HIREDIS_LIB")
    endif()

    target_link_libraries(ems_hiredis INTERFACE "${HIREDIS_LIB}")
endif()

if(WIN32)
    target_link_libraries(ems_hiredis INTERFACE ws2_32 crypt32)
endif()
