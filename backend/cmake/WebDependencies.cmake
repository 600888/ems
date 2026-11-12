include_guard(GLOBAL)

set(TRANTOR_USE_TLS
    "none"
    CACHE STRING "TLS provider for the bundled Trantor: none, openssl or botan"
)

# 用函数作用域隔离第三方选项；框架依赖始终随当前工具链和配置从源码构建。
function(ems_add_drogon)
    get_filename_component(third_party "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../third_party" ABSOLUTE)
    foreach(dependency IN ITEMS jsoncpp zlib drogon drogon/trantor)
        if(NOT EXISTS "${third_party}/${dependency}/CMakeLists.txt")
            message(FATAL_ERROR "Web dependency source missing: ${third_party}/${dependency}")
        endif()
    endforeach()

    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TESTING OFF)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_DOC OFF)

    set(JSONCPP_WITH_TESTS OFF)
    set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF)
    set(JSONCPP_WITH_EXAMPLE OFF)
    set(JSONCPP_STATIC_WINDOWS_RUNTIME OFF)
    set(BUILD_STATIC_LIBS ON)
    set(BUILD_OBJECT_LIBS OFF)
    add_subdirectory("${third_party}/jsoncpp" "${CMAKE_CURRENT_BINARY_DIR}/jsoncpp")

    set(ZLIB_BUILD_SHARED OFF)
    set(ZLIB_BUILD_STATIC ON)
    set(ZLIB_BUILD_TESTING OFF)
    add_subdirectory("${third_party}/zlib" "${CMAKE_CURRENT_BINARY_DIR}/zlib")
    add_library(ZLIB::ZLIB ALIAS zlibstatic)

    # Drogon 使用 find_package；覆盖当前作用域中的查找输入，避免复用旧安装包缓存。
    # ZLIB 保留 CMake 自带的查找模块，已有别名保证链接到本次构建的静态目标。
    set(ZLIB_INCLUDE_DIR "${third_party}/zlib")
    set(ZLIB_LIBRARY zlibstatic)
    set(ZLIB_LIBRARIES zlibstatic)
    set(jsoncpp_DIR "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/web")
    set(JSONCPP_INCLUDE_DIRS "${third_party}/jsoncpp/include")

    set(BUILD_CTL OFF)
    set(BUILD_ORM OFF)
    set(BUILD_BROTLI OFF)
    set(BUILD_YAML_CONFIG OFF)
    set(BUILD_C-ARES OFF)
    set(USE_SUBMODULE ON)
    set(USE_STATIC_LIBS_ONLY ON)
    add_subdirectory("${third_party}/drogon" "${CMAKE_CURRENT_BINARY_DIR}/drogon")
endfunction()
