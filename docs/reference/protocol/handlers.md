# 协议处理器接入

处理器位于 `backend/src/net/handler/`，与 `protocol/` 平级。`protocol/` 负责协议栈、协议对象和动态库 C ABI；`handler/` 负责应用配置、实例管理和采集结果转换。

```text
backend/src/net/
├── handler/
│   ├── protocol_handler.h       # 协议无关接口和 PointValue
│   ├── plugin_handler.h/.cpp    # 公共 ABI 适配器
│   ├── handler_factory.cpp     # 按协议创建处理器
│   ├── modbus/
│   │   ├── modbus_proto_handler.h/.cpp
│   │   └── modbus_handler.h/.cpp  # 存量 libmodbuspp 设备接口
│   ├── iec104/iec104_handler.h/.cpp
│   ├── dlt645/dlt645_handler.h/.cpp
│   └── base_handler.h/.cpp      # 存量 Modbus 基类
├── protocol/                    # 协议栈、插件和平台加载器
└── collect/                     # 采集引擎、通道线程、数据分发
```

统一接口为 `ems::net::handler::ProtocolHandler`，提供初始化、连接、断开、连接状态、批量读取、写值及错误查询。`ModbusProtoHandler` 支持 ModbusTCP / ModbusRTU；`Iec104Handler` 和 `Dlt645Handler` 分别调用对应动态库。三者都支持客户端和只读上送服务端，服务端 `write_value` 仅更新本地映像。

## 调用方式

```cpp
#include "net/handler/protocol_handler.h"
#include "net/protocol/plugin_manager.h"

ems::net::collect::PluginManager manager; // 必须比处理器活得更久。
manager.set_plugin_dir("./plugins");
auto handler = ems::net::handler::create_handler(manager, "DLT645");
if (!handler->initialize({{"connection", "127.0.0.1:6450"},
                          {"meter_address", "123456789012"},
                          {"timeout_ms", 1000}}) || !handler->connect()) {
    // 通过 handler->last_error() 获取诊断。
    return;
}
std::vector<ems::net::handler::PointValue> values;
const int valid = handler->read_batch({"0x02010100"}, values);
// valid 是有效点数，-1 表示调用失败；成功时每个输入都有对应的样本。
handler->disconnect();
```

调用方链接 `net_handler`，动态库按需加载，主程序不链接任一协议插件或第三方协议栈。扩展协议可先通过 `PluginManager::register_mapping` 注册，再使用同一工厂创建通用 `PluginHandler`。

配置接受顶层参数及 `settings` 对象 / JSON 字符串，顶层参数优先；`role=client/server/server_readonly` 与旧 `local_role` 兼容。已连接实例禁止重配置；断开后若新配置校验失败，原实例仍可重连。读取保持编码顺序，将数值和布尔值转换成统一样本，并在 `PointValue::details` 保留协议原始字段，例如 IEC 品质、DLT645 需量时间和复合字段。具体地址与参数见对应协议文档。

处理器在主进程内使用 C++ 接口，不属于动态库 ABI；跨动态库调用继续使用现有 C ABI。每个处理器内部串行执行操作；销毁前调用方须结束在途调用，最后释放实例及库租约。停止失败的资源由插件管理器保留，避免提前卸载。

## 采集与构建

`ChannelThread::start(PluginManager&)` 根据通道协议创建处理器，初始化失败同步返回 `false`；连接重试、采集及命令下发均经过处理器。通道退出释放实例，即使外部仍持有已停止的通道对象也不会延迟正常库卸载。管理器须在通道 `stop()` / `join()` 完成后销毁。

主工程按 `protocol → handler → collect` 顺序构建。独立验证 handler 时可只启用某个插件，例如仅 IEC104：

```sh
cmake -S backend/src/net/handler -B build/handlers \
  -DEMS_BUILD_MODBUS=OFF -DEMS_BUILD_DLT645=OFF
cmake --build build/handlers
ctest --test-dir build/handlers --output-on-failure
```

启用 Modbus 或 DLT645 时，分别提供同工具链 libmodbus 或 Boost 1.83+ 头文件；依赖参数见 [Windows 构建](../../../README.md#windows-构建) 和 [DLT645 文档](dlt645.md)。多配置生成器编译、测试需指定相同配置，例如 `--config Release` / `-C Release`。

存量 `ems::net::device` Modbus 接口也已移到 handler 目录，保持原调用语义，由 `EMS_BUILD_LEGACY_MODBUS` 控制。其基类带有 Modbus 地址字段，新协议使用上述协议无关接口。
