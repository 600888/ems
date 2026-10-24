# 第三方库入口：仅调整集成选项，不修改第三方源码及其格式。
include(OpenXLSX)
ems_add_openxlsx()

if(EMS_BUILD_LEGACY_MODBUS)
    add_subdirectory(libmodbuspp)
endif()

# 与 MSVC 默认动态 CRT 保持一致，避免测试目标出现运行库冲突。
if(BUILD_TESTS)
    set(gtest_force_shared_crt
        ON
        CACHE BOOL "Use the shared MSVC runtime" FORCE
    )

    add_subdirectory(googletest)
endif()

# IEC104 的 C 栈由协议模块集成，不构建第三方 Python 绑定。

# 只构建后端使用的共享客户端；上游测试需要额外 Redis 服务。
set(REDIS_PLUS_PLUS_BUILD_TEST
    OFF
    CACHE BOOL "Build redis-plus-plus tests"
)
set(REDIS_PLUS_PLUS_BUILD_STATIC
    OFF
    CACHE BOOL "Build static redis-plus-plus"
)

add_subdirectory(redis-plus-plus)

if(EMS_BUILD_WEB)
    # 保留父工程开关；Drogon 只提供 HTTP，数据库仍由公共层管理。
    function(ems_add_drogon)
        set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
        set(CMAKE_CXX_STANDARD 17)
        set(BUILD_CTL OFF)
        set(BUILD_ORM OFF)
        set(BUILD_TESTING OFF)
        set(BUILD_EXAMPLES OFF)
        set(BUILD_BROTLI OFF)
        set(BUILD_YAML_CONFIG OFF)
        set(BUILD_SHARED_LIBS OFF)
        set(USE_STATIC_LIBS_ONLY ON)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/drogon/trantor/CMakeLists.txt")
            set(USE_SUBMODULE ON)
        else()
            set(USE_SUBMODULE OFF)
            find_package(Trantor CONFIG REQUIRED)
        endif()
        add_subdirectory(drogon)
    endfunction()
    ems_add_drogon()
endif()
