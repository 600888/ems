include_guard(GLOBAL)

# 运行交付物与静态库、导入库等链接产物分开；多配置生成器自动追加配置名。
function(ems_set_output_directories root)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
        "${root}/bin"
        PARENT_SCOPE
    )
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
        "${root}/lib"
        PARENT_SCOPE
    )
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY
        "${root}/archive"
        PARENT_SCOPE
    )
    set(CMAKE_PDB_OUTPUT_DIRECTORY
        "${root}/symbols"
        PARENT_SCOPE
    )
endfunction()

# 部分上游项目写死目标输出路径，在集成层覆盖，保持第三方源码不变。
function(ems_set_target_output_directories target root)
    set_target_properties(
        ${target}
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${root}/bin"
                   LIBRARY_OUTPUT_DIRECTORY "${root}/lib"
                   ARCHIVE_OUTPUT_DIRECTORY "${root}/archive"
                   PDB_OUTPUT_DIRECTORY "${root}/symbols"
                   COMPILE_PDB_OUTPUT_DIRECTORY "${root}/symbols"
    )
endfunction()

# 在目标所属目录调用，将运行依赖复制到目标文件旁。
function(ems_copy_runtime_files target)
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND
            ${CMAKE_COMMAND} "-DEMS_RUNTIME_DLLS=${ARGN}"
            "-DEMS_RUNTIME_DESTINATION=$<TARGET_FILE_DIR:${target}>" -P
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CopyRuntimeDependencies.cmake"
        VERBATIM
    )
endfunction()

# 在目标所属目录调用，保证测试发现与命令行启动使用一致的库目录。
function(ems_copy_directory_runtime_dependencies)
    if(NOT WIN32)
        return()
    endif()

    get_property(
        targets
        DIRECTORY
        PROPERTY BUILDSYSTEM_TARGETS
    )
    foreach(target IN LISTS targets)
        get_target_property(type ${target} TYPE)
        if(type STREQUAL "EXECUTABLE")
            ems_copy_runtime_files(${target} "$<TARGET_RUNTIME_DLLS:${target}>")
        endif()
    endforeach()
endfunction()
