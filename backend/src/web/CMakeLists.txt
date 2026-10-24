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
        find_file(
            EMS_WEB_ZLIB_RUNTIME
            NAMES zlib.dll zlib1.dll
            HINTS "${zlib_prefix}/bin"
        )
        if(EMS_WEB_ZLIB_RUNTIME)
            add_custom_command(
                TARGET ${target}
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${EMS_WEB_ZLIB_RUNTIME}"
                        "$<TARGET_FILE_DIR:${target}>"
            )
        endif()
    endif()
endfunction()
ems_copy_web_runtime(ems-web)
