# 缺少安装包时，用当前工具链构建固定源码，避免 MSVC / MinGW 库混用。
function(ems_add_hiredis)
    include(FetchContent)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    set(BUILD_SHARED_LIBS ON)
    set(DISABLE_TESTS ON)
    set(ENABLE_EXAMPLES OFF)
    set(ENABLE_SSL OFF)
    set(ENABLE_NUGET OFF)

    fetchcontent_declare(
        ems_hiredis
        URL https://codeload.github.com/redis/hiredis/tar.gz/refs/tags/v1.3.0
        URL_HASH SHA256=25cee4500f359cf5cad3b51ed62059aadfc0939b05150c1f19c7e2829123631c
    )
    fetchcontent_makeavailable(ems_hiredis)

    if(MINGW)
        # hiredis 的单调时钟调用由 MinGW 的 winpthreads 提供。
        target_link_libraries(hiredis PRIVATE winpthread)
    endif()

    # 上游源码平铺头文件，redis-plus-plus 使用 hiredis/*.h 的安装布局。
    set(header_root "${ems_hiredis_BINARY_DIR}/include")
    file(
        COPY "${ems_hiredis_SOURCE_DIR}/"
        DESTINATION "${header_root}/hiredis"
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN ".git" EXCLUDE
    )
    target_include_directories(hiredis SYSTEM PUBLIC $<BUILD_INTERFACE:${header_root}>)

    # redis-plus-plus 的查找脚本也能复用源码目标，无需查找尚未生成的库文件。
    set(HIREDIS_HEADER "${header_root}" PARENT_SCOPE)
    set(HIREDIS_LIB hiredis::hiredis PARENT_SCOPE)
endfunction()
