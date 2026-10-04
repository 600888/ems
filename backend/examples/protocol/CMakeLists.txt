# 存量 Modbus C++ 包装器示例仅在对应库启用时构建。
if(TARGET modbus_protocol)
    add_subdirectory(modbus)
endif()
