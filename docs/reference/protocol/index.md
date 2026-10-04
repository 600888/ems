# 协议库参考

- [通讯协议设计](../../processes/collector/protocol-design.md)：PIMPL API、按需动态加载与跨平台契约。
- [协议处理器接入](handlers.md)：独立 handler 目录、三个协议处理器、统一工厂及采集线程生命周期。
- [IEC 104 动态库接入](iec104.md)：客户端缓存、控制确认、只读上送、配置和独立构建。
- [DLT645 动态库接入](dlt645.md)：TCP/RTU、00–04 类 DI、复合字段、只读上送和模拟器互通。
- [libmodbuspp 使用指南](libmodbuspp-guide.md)：客户端、服务端、数据类型、地址和字序示例。

已合并原先位于两个目录的相同副本。指南不声明 EMS 已完成相应能力，不替代 [通道协议契约](../../architecture/channel-protocol.md)。
