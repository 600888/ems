# Source directories
add_subdirectory(common)

add_subdirectory(util)

add_subdirectory(rtdb)

add_subdirectory(net)

add_subdirectory(scd)

# 守护进程跨平台：Linux 走 POSIX 进程接口，Windows 走 CreateProcess/控制台事件。
if(EMS_BUILD_DAEMON AND (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    add_subdirectory(daemon)
elseif(EMS_BUILD_DAEMON)
    message(STATUS "ems-daemon 仅支持 Windows 与 Linux，当前平台已跳过")
endif()

if(EMS_BUILD_WEB)
    add_subdirectory(web)
endif()
