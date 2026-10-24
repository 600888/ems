# 仓库保留了上游 OpenXLSX 库目录；在此补齐其顶层的依赖和版本变量。
# 函数作用域避免第三方的 BUILD_EXAMPLES / BUILD_SHARED_LIBS 影响后端。
set(_ems_openxlsx_module_dir "${CMAKE_CURRENT_LIST_DIR}")
function(ems_add_openxlsx)
    if(TARGET OpenXLSX::OpenXLSX)
        return()
    endif()

    include(FetchContent)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_TESTING OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)

    find_package(pugixml 1.15 CONFIG QUIET)
    if(NOT TARGET pugixml::pugixml)
        FetchContent_Declare(
            ems_pugixml
            URL https://codeload.github.com/zeux/pugixml/tar.gz/refs/tags/v1.15
            URL_HASH SHA256=b39647064d9e28297a34278bfb897092bf33b7c487906ddfc094c9e8868bddcb
        )
        FetchContent_MakeAvailable(ems_pugixml)
        set(PUGIXML_INSTALL_TARGET pugixml-static)
        set(PUGIXML_TARGET pugixml::static)
    else()
        set(PUGIXML_TARGET pugixml::pugixml)
    endif()

    find_package(miniz 3.0.2 CONFIG QUIET)
    if(NOT TARGET miniz::miniz)
        set(INSTALL_PROJECT ON)
        FetchContent_Declare(
            ems_miniz
            URL https://codeload.github.com/richgel999/miniz/tar.gz/refs/tags/3.0.2
            URL_HASH SHA256=c4b4c25a4eb81883448ff8924e6dba95c800094a198dc9ce66a292ac2ef8e018
        )
        FetchContent_MakeAvailable(ems_miniz)
        set(ZIP_LIBRARY_PROVIDED_TARGET miniz)
        set(ZIP_LIBRARY_INSTALL_TARGET miniz)
    else()
        set(ZIP_LIBRARY_PROVIDED_TARGET miniz::miniz)
    endif()
    set(ZIP_LIBRARY miniz)
    set(OPENXLSX_ENABLE_LIBZIP OFF)

    if(WIN32)
        find_package(nowide 11.3.1 CONFIG QUIET)
        if(NOT TARGET nowide::nowide)
            set(NOWIDE_INSTALL ON)
            FetchContent_Declare(
                ems_nowide
                URL https://github.com/boostorg/nowide/releases/download/v11.3.1/nowide_standalone_v11.3.1.tar.gz
                URL_HASH SHA256=eaec4d331e3961f5eeb10c46a11691d62047900a7a40765b0f23cdd3181e6ca6
            )
            FetchContent_MakeAvailable(ems_nowide)
            set(NOWIDE_TARGET nowide)
        else()
            set(NOWIDE_TARGET nowide::nowide)
        endif()
        set(NOWIDE_ALIAS nowide::nowide)
        set(OPENXLSX_ENABLE_NOWIDE ON)
    endif()

    set(OPENXLSX_MAJOR_VERSION 0)
    set(OPENXLSX_MINOR_VERSION 5)
    set(OPENXLSX_MICRO_VERSION 1)
    set(OpenXLSX_VERSION 0.5.1)
    set(OPENXLSX_LIBRARY_NAME OpenXLSX)
    set(OPENXLSX_LIBRARY_TYPE STATIC)
    set(OPENXLSX_ENABLE_LTO OFF)
    set(OPENXLSX_MONOLITHIC_LIBRARY OFF)
    add_subdirectory(
        "${_ems_openxlsx_module_dir}/../third_party/OpenXLSX"
        "${CMAKE_CURRENT_BINARY_DIR}/OpenXLSX"
    )
endfunction()
