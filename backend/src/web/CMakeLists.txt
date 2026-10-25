# 各模块自己拥有源码集合；新增文件由 CMake 自动检测，无需手工维护 cpp 清单。
function(ems_web_library target directory)
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${directory}/*.cpp")
    add_library(${target} STATIC ${sources})
    target_include_directories(
        ${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/.."
                         "${CMAKE_SOURCE_DIR}/third_party/libmodbuspp/3rdparty/json/single_include"
    )
    target_link_libraries(${target} PUBLIC ems_common_core)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
endfunction()

ems_web_library(ems_web_transport "${CMAKE_CURRENT_SOURCE_DIR}/transport")
ems_web_library(ems_web_platform "${CMAKE_CURRENT_SOURCE_DIR}/platform")
ems_web_library(ems_web_identity "${CMAKE_CURRENT_SOURCE_DIR}/modules/identity")
ems_web_library(ems_web_device "${CMAKE_CURRENT_SOURCE_DIR}/modules/device")
ems_web_library(ems_web_health "${CMAKE_CURRENT_SOURCE_DIR}/modules/health")
ems_web_library(ems_web_app "${CMAKE_CURRENT_SOURCE_DIR}/app")

target_link_libraries(ems_web_platform PUBLIC ems_web_transport Threads::Threads)
if(WIN32)
    target_link_libraries(ems_web_platform PRIVATE bcrypt)
else()
    find_package(OpenSSL REQUIRED)
    target_link_libraries(ems_web_platform PRIVATE OpenSSL::Crypto)
endif()
target_link_libraries(ems_web_identity PUBLIC ems_web_platform)
target_link_libraries(ems_web_device PUBLIC ems_web_identity ems_database_mysql ems_config_core)
if(TARGET miniz::miniz)
    target_link_libraries(ems_web_device PRIVATE miniz::miniz)
else()
    target_link_libraries(ems_web_device PRIVATE miniz)
endif()
target_link_libraries(ems_web_health PUBLIC ems_web_transport)
target_link_libraries(ems_web_app PUBLIC ems_web_device ems_web_health drogon)

file(GLOB entry_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_executable(ems-web ${entry_sources})
target_include_directories(ems-web PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(ems-web PRIVATE ems_web_app Threads::Threads)
set_target_properties(ems-web PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
ems_copy_mysql_runtime(ems-web)

# 某些 ZLIB 包提供导入库，Windows 构建不能依赖开发终端 PATH 中的其他 zlib.dll。
function(ems_copy_web_runtime target)
    if(WIN32)
        get_filename_component(zlib_prefix "${ZLIB_INCLUDE_DIR}" DIRECTORY)
        # 优先选择实际 ZLIB 包的 DLL，替换旧缓存中从 PATH 误找到的其他工具链 DLL。
        set(_ems_zlib_runtime_found OFF)
        foreach(_ems_zlib_dll IN ITEMS zlib.dll zlib1.dll libzlib.dll)
            if(EXISTS "${zlib_prefix}/bin/${_ems_zlib_dll}")
                set(EMS_WEB_ZLIB_RUNTIME
                    "${zlib_prefix}/bin/${_ems_zlib_dll}"
                    CACHE FILEPATH "ZLIB runtime DLL deployed with Web targets" FORCE
                )
                set(_ems_zlib_runtime_found ON)
                break()
            endif()
        endforeach()
        if(NOT _ems_zlib_runtime_found AND (NOT EMS_WEB_ZLIB_RUNTIME OR NOT EXISTS
                                                                        "${EMS_WEB_ZLIB_RUNTIME}")
        )
            unset(EMS_WEB_ZLIB_RUNTIME CACHE)
            unset(EMS_WEB_ZLIB_RUNTIME)
        endif()
        if(EMS_WEB_ZLIB_RUNTIME)
            ems_copy_runtime_files(${target} "${EMS_WEB_ZLIB_RUNTIME}")
        endif()
    endif()
endfunction()
ems_copy_web_runtime(ems-web)
ems_copy_directory_runtime_dependencies()
