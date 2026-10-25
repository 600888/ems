# 空依赖列表无需调用 copy_if_different，避免纯静态测试的构建后步骤失败。
if(EMS_RUNTIME_DLLS)
    file(MAKE_DIRECTORY "${EMS_RUNTIME_DESTINATION}")
endif()
foreach(runtime_dll IN LISTS EMS_RUNTIME_DLLS)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${runtime_dll}"
                "${EMS_RUNTIME_DESTINATION}" COMMAND_ERROR_IS_FATAL ANY
    )
endforeach()
