# ems-daemon：EMS 子进程守护与管理器。
#
# 平台相关实现分文件编译：
#   process_posix.cpp — Linux：fork/execve/waitpid/pid 文件锁/信号；
#   process_win32.cpp — Windows：CreateProcessW/WaitForSingleObject/命名互斥量/控制台事件。
# 公共逻辑（配置解析、重启策略、主循环）与平台无关。
set(EMS_DAEMON_COMMON_SRC process.cpp process_manager.cpp pid_file.cpp daemon_log.cpp)
if(WIN32)
    set(EMS_DAEMON_PLATFORM_SRC process_win32.cpp)
else()
    set(EMS_DAEMON_PLATFORM_SRC process_posix.cpp)
endif()

add_executable(ems-daemon ${EMS_DAEMON_COMMON_SRC} ${EMS_DAEMON_PLATFORM_SRC} main.cpp)

# 头文件来源：公共接口（include/）、进程内公共实现（src/log 等）、
# 以及 spdlog、nlohmann/json 两个纯头文件第三方库。
target_include_directories(
    ems-daemon
    PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/third_party
            ${CMAKE_SOURCE_DIR}/third_party/libmodbuspp/3rdparty/json/single_include
)

find_package(Threads REQUIRED)

target_link_libraries(ems-daemon PRIVATE Threads::Threads)

set_target_properties(
    ems-daemon
    PROPERTIES CXX_STANDARD 20
               CXX_STANDARD_REQUIRED YES
               CXX_EXTENSIONS NO
)

ems_copy_directory_runtime_dependencies()
