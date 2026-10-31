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

    function(ems_add_googletest)
        set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
        set(INSTALL_GTEST OFF)
        ems_set_output_directories("${CMAKE_BINARY_DIR}/tests")
        add_subdirectory(googletest)
        foreach(target IN ITEMS gtest gtest_main gmock gmock_main)
            ems_set_target_output_directories(${target} "${CMAKE_BINARY_DIR}/tests")
        endforeach()
    endfunction()
    ems_add_googletest()
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
    include(Argon2)
    include(WebDependencies)
    # 保留父工程开关；Drogon 只提供 HTTP，数据库仍由公共层管理。
    function(ems_add_drogon)
        ems_prepare_web_dependency_prefix()
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
            find_package(Trantor CONFIG QUIET)
            if(NOT TARGET Trantor::Trantor)
                if(MSVC)
                    set(_ems_web_setup_toolchain msvc)
                elseif(MINGW)
                    set(_ems_web_setup_toolchain mingw)
                endif()
                if(WIN32 AND _ems_web_setup_toolchain)
                    message(
                        FATAL_ERROR
                            "Trantor is missing. From the repository root, run "
                            "scripts/setup_web_dependencies.ps1 -Toolchain ${_ems_web_setup_toolchain} "
                            "in the matching compiler terminal, then configure again. "
                            "For an external installation, set EMS_WEB_DEPENDENCY_PREFIX "
                            "or CMAKE_PREFIX_PATH to its installation prefix."
                    )
                else()
                    message(
                        FATAL_ERROR
                            "Trantor is missing. Install Trantor, JsonCpp and zlib for the target "
                            "toolchain and set EMS_WEB_DEPENDENCY_PREFIX or CMAKE_PREFIX_PATH."
                    )
                endif()
            endif()
        endif()
        add_subdirectory(drogon)
    endfunction()
    ems_add_drogon()
endif()
