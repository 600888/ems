# 协议插件、采集引擎和参数加载模块。

add_subdirectory(protocol)

# 处理器是协议插件的调用层，独立于各协议栈实现。
add_subdirectory(handler)

add_subdirectory(collect)

add_subdirectory(para_loader)
