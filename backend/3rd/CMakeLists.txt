# 第三方库入口：仅调整集成选项，不修改第三方源码及其格式。
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
