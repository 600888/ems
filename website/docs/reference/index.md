# 开发参考

开发参考用于理解第三方库和编写原型，不是 EMS 架构的另一份定义。API、构建与平台能力需在选定依赖版本上验证。

- [协议库参考](protocol/index.md)：客户端/服务端的第三方 API 使用示例。
- [代码格式化规范](code-format.md)：clang-format 规则、固定版本、脚本与 CMake 目标用法，以及不参与格式化的范围。

先遵循 [设备、通道与协议](../architecture/channel-protocol.md) 的模型、权限和控制闭环，再选择库调用；不能因为 SDK 提供写操作就绕过仲裁。
