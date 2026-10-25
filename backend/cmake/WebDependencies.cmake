include_guard(GLOBAL)

set(EMS_WEB_DEPENDENCY_PREFIX
    ""
    CACHE PATH "Optional installation prefix for Trantor, JsonCpp and zlib"
)

# 依赖准备脚本使用固定的工具链目录，普通配置和 CMake Tools 都能直接复用。
function(ems_prepare_web_dependency_prefix)
    if(EMS_WEB_DEPENDENCY_PREFIX)
        list(PREPEND CMAKE_PREFIX_PATH "${EMS_WEB_DEPENDENCY_PREFIX}")
    elseif(WIN32 AND (MSVC OR MINGW))
        if(MSVC)
            set(_ems_web_toolchain msvc)
        else()
            set(_ems_web_toolchain mingw)
        endif()
        get_filename_component(
            _ems_web_backend_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE
        )
        set(_ems_web_local_prefix
            "${_ems_web_backend_root}/build/web-deps-${_ems_web_toolchain}/install"
        )
        if(IS_DIRECTORY "${_ems_web_local_prefix}")
            # 显式传入的外部安装前缀优先，本地同工具链依赖作为补充。
            list(APPEND CMAKE_PREFIX_PATH "${_ems_web_local_prefix}")
        endif()
    endif()
    list(REMOVE_DUPLICATES CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH
        "${CMAKE_PREFIX_PATH}"
        PARENT_SCOPE
    )
endfunction()
