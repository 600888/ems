# 数据采集架构设计文档

## 1. 概述

### 1.1 设计目标

数据采集模块是 EMS 系统的核心组件，负责通过多种工业通信协议与现场设备交互，完成遥测（YC）、遥信（YX）、遥控（YK）、遥调（YT）四类数据的采集与下发。

本设计基于以下核心原则：

- **一个通道一个线程**：每个通道（Channel）独占一个工作线程，通道间互不干扰
- **规约动态库按需加载**：每种协议编译为独立的 `.so` 动态库，运行时按需 `dlopen` 加载，未使用的协议不占用内存
- **统一接口抽象**：所有协议插件实现统一的 C 接口，采集引擎不依赖任何具体协议实现

### 1.2 与 Python 版架构的对比

| 维度 | Python 版 (ems_simulate) | C++ 版 (本设计) |
|------|-------------------------|----------------|
| 协议加载 | import 静态导入所有 handler | dlopen 按需加载 .so 插件 |
| 线程模型 | asyncio 事件循环 + 线程池 | 每通道独立 std::thread |
| 接口定义 | Python ABC 抽象基类 | 纯 C 虚函数 + C 导出符号 |
| 数据存储 | 内存字典 + SQLite | Redis 实时库 + RTDB |
| 扩展方式 | 新增 handler.py 文件 | 新增 .so 动态库，无需重编译主程序 |

### 1.3 支持的协议

| 协议 | 传输方式 | 默认端口 | 典型设备 | 动态库文件名 |
|------|---------|---------|---------|-------------|
| Modbus TCP/RTU | TCP / RS485 | 502 | PCS、BMS、通用工控设备 | `libproto_modbus.so` |
| DL/T 645-2007 | TCP / RS485 | 8899 | 电能表 | `libproto_dlt645.so` |
| IEC 60870-5-104 | TCP | 2404 | 电力远动设备、保护装置 | `libproto_iec104.so` |
| IEC 61850 | TCP (MMS) | 102 | 变电站 IED、GOOSE | `libproto_iec61850.so` |

---

## 2. 整体架构

### 2.1 架构图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          采集进程 (collect)                              │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                     采集引擎 (CollectEngine)                     │   │
│  │                                                                 │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │   │
│  │  │ 通道线程 #1   │  │ 通道线程 #2   │  │ 通道线程 #N   │  ...     │   │
│  │  │              │  │              │  │              │          │   │
│  │  │ ChannelCtx   │  │ ChannelCtx   │  │ ChannelCtx   │          │   │
│  │  │  ┌────────┐  │  │  ┌────────┐  │  │  ┌────────┐  │          │   │
│  │  │  │Protocol│  │  │  │Protocol│  │  │  │Protocol│  │          │   │
│  │  │  │Plugin* │  │  │  │Plugin* │  │  │  │Plugin* │  │          │   │
│  │  │  └────────┘  │  │  └────────┘  │  │  └────────┘  │          │   │
│  │  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘          │   │
│  │         │                 │                 │                   │   │
│  └─────────┼─────────────────┼─────────────────┼───────────────────┘   │
│            │                 │                 │                        │
│  ┌─────────▼─────────────────▼─────────────────▼───────────────────┐   │
│  │                    插件管理器 (PluginManager)                     │   │
│  │                                                                 │   │
│  │   dlopen/dlsym 按需加载，统一生命周期管理                          │   │
│  │                                                                 │   │
│  │   ┌─────────────────┐  ┌─────────────────┐                      │   │
│   │   │ libproto_    │  │ libproto_    │  ...                 │   │
│   │   │ modbus.so     │  │ iec104.so     │                      │   │
│  │   └─────────────────┘  └─────────────────┘                      │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                    数据分发层 (DataDispatcher)                    │   │
│  │                                                                 │   │
│  │   采集数据 ──▶ RTDB 写入 ──▶ Redis Pub/Sub ──▶ 存储进程/策略进程  │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                    参数加载器 (ParaLoader)                        │   │
│  │                                                                 │   │
│  │   从关系数据库加载 通道/设备/测点 配置，初始化运行时参数             │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.2 核心组件职责

| 组件 | 职责 |
|------|------|
| **CollectEngine** | 采集引擎主类，管理所有通道线程的创建/销毁/启停，协调插件加载与数据分发 |
| **ChannelThread** | 每个通道对应一个独立线程，持有该通道的协议插件实例，执行轮询采集循环 |
| **PluginManager** | 管理动态库的加载/卸载，维护协议名称到 `.so` 的映射，提供插件工厂方法 |
| **ProtocolPlugin** | 协议插件抽象接口，所有 `.so` 必须实现此接口的 C 导出函数 |
| **DataDispatcher** | 将采集到的数据写入 RTDB，并通过 Redis Pub/Sub 通知其他进程 |
| **ParaLoader** | 启动时从关系数据库加载通道、设备、测点配置 |

---

## 3. 通道线程模型

### 3.1 一个通道一个线程

每个通道（Channel）对应一个独立的 `std::thread`，通道内所有设备共享该线程进行数据采集。

```
┌──────────────────────────────────────────────────────────────┐
│                     通道线程 (ChannelThread)                   │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  ChannelConfig:                                              │
│    id = 1                                                    │
│    name = "Modbus-TCP-1"                                     │
│    protocol = ModbusTCP                                      │
│    connection = "192.168.1.10:502"                           │
│                                                              │
│  ┌────────────────────────────────────────────────────────┐  │
│  │              协议插件实例 (ProtocolPlugin*)              │  │
│  │                                                        │  │
│  │  connect() ──▶ 成功后进入采集循环                        │  │
│  │                                                        │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐             │  │
│  │  │ 设备 #1   │  │ 设备 #2   │  │ 设备 #3   │             │  │
│  │  │ (slave=1)│  │ (slave=2)│  │ (slave=3)│             │  │
│  │  │          │  │          │  │          │             │  │
│  │  │ YC: 50点 │  │ YC: 30点 │  │ YC: 20点 │             │  │
│  │  │ YX: 20点 │  │ YX: 10点 │  │ YX: 15点 │             │  │
│  │  │ YK: 5点  │  │ YK: 3点  │  │ YK: 2点  │             │  │
│  │  └──────────┘  └──────────┘  └──────────┘             │  │
│  └────────────────────────────────────────────────────────┘  │
│                                                              │
│  采集循环:                                                    │
│  while (running_) {                                          │
│      for (device : devices) {                                │
│          plugin->read_batch(device.yc_points);   // 批量遥测  │
│          plugin->read_batch(device.yx_points);   // 批量遥信  │
│          dispatcher_->dispatch(device.points);   // 数据分发  │
│      }                                                      │
│      sleep(poll_interval_);                                  │
│  }                                                          │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### 3.2 线程隔离的好处

1. **故障隔离**：某个通道的通信异常（如网络断开、设备无响应）不会影响其他通道
2. **协议隔离**：不同协议的阻塞调用（如串口 I/O）不会互相阻塞
3. **独立调度**：每个通道可以配置不同的采集周期和策略
4. **资源可控**：每个通道的内存和 CPU 使用独立可监控

### 3.3 通道线程生命周期

```
                    ┌──────────┐
                    │  Created │
                    └────┬─────┘
                         │ start()
                    ┌────▼─────┐
           ┌───────│ Starting │──────────┐
           │       └────┬─────┘          │
           │ 加载插件    │ connect()      │ 加载失败
           │ 失败       │ 成功            │
           │       ┌────▼─────┐    ┌─────▼──────┐
           │       │ Running  │    │  Error     │
           │       └────┬─────┘    └─────┬──────┘
           │            │ stop()         │ 重试/恢复
           │       ┌────▼─────┐          │
           └──────▶│ Stopping │◀─────────┘
                   └────┬─────┘
                        │
                   ┌────▼─────┐
                   │ Stopped  │
                   └──────────┘
```

### 3.4 通道上下文结构

```cpp
struct ChannelContext {
    int64_t channel_id;
    ChannelConfig config;
    std::unique_ptr<std::thread> thread;
    std::atomic<bool> running{false};

    ProtocolPlugin* plugin{nullptr};       // 由 PluginManager 创建
    void* plugin_handle{nullptr};          // dlopen 返回的句柄

    std::vector<DeviceContext> devices;    // 通道下的设备列表
    int poll_interval_ms{1000};            // 采集周期

    std::mutex cmd_mutex;                  // 遥控/遥调命令队列锁
    std::deque<CommandTask> cmd_queue;     // 下发命令队列
};
```

---

## 4. 动态库插件机制

### 4.1 设计思路

参考 Python 版 `ProtocolHandler` 抽象基类的设计，C++ 版将协议接口定义为纯虚类 + C 导出函数。每种协议编译为独立的 `.so` 文件，采集引擎通过 `dlopen`/`dlsym` 在运行时按需加载。

**关键原则：用到时才加载，不用不加载。**

```
┌──────────────────────────────────────────────────────────────┐
│                    插件加载流程                                │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  1. ParaLoader 加载通道配置，发现通道使用 ModbusTCP 协议       │
│                     │                                        │
│  2. CollectEngine 查询 PluginManager: "ModbusTCP" 是否已加载  │
│                     │                                        │
│         ┌───────────┴───────────┐                            │
│         │ 已加载                 │ 未加载                      │
│         ▼                       ▼                            │
│  返回已缓存的           3. dlopen("libproto_modbus.so")       │
│  插件工厂函数                │                                │
│                    4. dlsym("ems_plugin_create")              │
│                              │                                │
│                    5. 调用 create() 获取插件实例               │
│                              │                                │
│                    6. 缓存 {协议名 -> dlopen句柄, 工厂函数}    │
│                              │                                │
│                    7. 返回插件实例给通道线程                    │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### 4.2 插件 C 接口定义

每个 `.so` 必须导出以下 C 函数符号：

```cpp
// protocol_plugin_api.h - 插件标准接口

#ifdef __cplusplus
extern "C" {
#endif

#define EMS_PLUGIN_API_VERSION 1

// 插件信息
struct EmPluginInfo {
    int api_version;
    const char* protocol_name;     // "ModbusTCP", "IEC104", "DLT645", "IEC61850"
    const char* description;
    const char* version;
};

// 插件必须导出的函数符号

// 获取插件信息
typedef const EmPluginInfo* (*ems_plugin_info_fn)();

// 创建插件实例（返回不透明指针）
typedef void* (*ems_plugin_create_fn)();

// 销毁插件实例
typedef void (*ems_plugin_destroy_fn)(void* instance);

// 初始化插件（传入 JSON 配置）
typedef int (*ems_plugin_init_fn)(void* instance, const char* json_config);

// 连接设备
typedef int (*ems_plugin_connect_fn)(void* instance);

// 断开连接
typedef int (*ems_plugin_disconnect_fn)(void* instance);

// 批量读取测点值
typedef int (*ems_plugin_read_batch_fn)(
    void* instance,
    const char* point_codes[],     // 测点编码数组
    int point_count,
    char* result_json,             // 输出: JSON 结果
    int result_buf_size
);

// 写入单个测点值
typedef int (*ems_plugin_write_fn)(
    void* instance,
    const char* point_code,
    double value
);

// 获取连接状态
typedef int (*ems_plugin_is_connected_fn)(void* instance);

// 获取最近错误信息
typedef const char* (*ems_plugin_last_error_fn)(void* instance);

#ifdef __cplusplus
}
#endif
```

### 4.3 插件内部 C++ 抽象类

每个 `.so` 内部使用 C++ 抽象类实现协议逻辑，仅通过 C 导出函数对外暴露：

```cpp
// protocol_handler.h - 插件内部抽象基类

class ProtocolHandler {
public:
    virtual ~ProtocolHandler() = default;

    virtual bool initialize(const json& config) = 0;
    virtual bool connect() = 0;
    virtual bool disconnect() = 0;
    virtual bool is_connected() const = 0;

    // 批量读取（优化网络 I/O）
    virtual int read_batch(
        const std::vector<std::string>& point_codes,
        std::vector<PointValue>& results
    ) = 0;

    // 单点写入
    virtual bool write_value(const std::string& point_code, double value) = 0;

    virtual std::string last_error() const = 0;
};
```

### 4.4 各协议动态库实现概要

#### 4.4.1 libproto_modbus.so

```
┌──────────────────────────────────────────────────────────────┐
│                  libproto_modbus.so                       │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  导出符号:                                                    │
│    ems_plugin_info()     → {"ModbusTCP/RTU", v1.0}           │
│    ems_plugin_create()   → ModbusHandler 实例                │
│    ems_plugin_destroy()  → 释放实例                          │
│    ems_plugin_init()     → 初始化连接参数                     │
│    ems_plugin_connect()  → 建立 TCP/串口 连接                │
│    ems_plugin_read_batch() → 批量读寄存器                    │
│    ems_plugin_write()    → 写单个寄存器/线圈                 │
│                                                              │
│  内部实现:                                                    │
│    ModbusTcpHandler  - Modbus TCP 客户端/服务端              │
│    ModbusRtuHandler  - Modbus RTU 串口客户端/服务端          │
│                                                              │
│  依赖: libmodbus / libmodbuspp                               │
│                                                              │
│  配置参数 (JSON):                                             │
│    {                                                         │
│      "mode": "client|server",                                │
│      "transport": "tcp|rtu|rtu_over_tcp",                    │
│      "ip": "192.168.1.10",                                   │
│      "port": 502,                                            │
│      "serial_port": "/dev/ttyS0",                            │
│      "baudrate": 9600,                                       │
│      "parity": "N",                                          │
│      "databits": 8,                                          │
│      "stopbits": 1,                                          │
│      "timeout_ms": 1000,                                     │
│      "slave_id": 1                                           │
│    }                                                         │
│                                                              │
│  采集优化:                                                    │
│    - 连续地址合并：将相邻寄存器合并为一次读请求                │
│    - 按功能码分组：线圈(01/02)与寄存器(03/04)分开读取         │
│    - 按 slave_id 分组：同一从站的请求合并                     │
│                                                              │
│  测点地址格式: "slave_id:func_code:address[:decode]"          │
│    示例: "1:3:0:FLOAT_BE"  (从站1, 功能码3, 地址0, 32位浮点)  │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

#### 4.4.2 libproto_dlt645.so

```
┌──────────────────────────────────────────────────────────────┐
│                  libproto_dlt645.so                       │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  导出符号:                                                    │
│    ems_plugin_info()     → {"DLT645", v1.0}                  │
│    ems_plugin_create()   → DLT645Handler 实例                │
│    ems_plugin_destroy()  → 释放实例                          │
│    ems_plugin_init()     → 初始化连接参数                     │
│    ems_plugin_connect()  → 建立 TCP/串口 连接                │
│    ems_plugin_read_batch() → 按 DI 标识批量读取              │
│    ems_plugin_write()    → 写参变量 (DI 前缀 04)             │
│                                                              │
│  内部实现:                                                    │
│    DLT645TcpHandler   - TCP 透传模式                         │
│    DLT645RtuHandler   - RS485 串口直连                       │
│                                                              │
│  依赖: 自研 dlt645 协议库                                    │
│                                                              │
│  配置参数 (JSON):                                             │
│    {                                                         │
│      "mode": "client|server",                                │
│      "transport": "tcp|rtu",                                 │
│      "ip": "192.168.1.20",                                   │
│      "port": 8899,                                           │
│      "serial_port": "/dev/ttyUSB0",                          │
│      "baudrate": 9600,                                       │
│      "parity": "E",                                          │
│      "databits": 8,                                          │
│      "stopbits": 1,                                          │
│      "meter_address": "000000001234",                        │
│      "timeout_ms": 3000                                      │
│    }                                                         │
│                                                              │
│  采集特点:                                                    │
│    - 按 DI 数据标识读取，DI 前缀决定读取方法                  │
│      00: 电能, 01: 最大需量, 02: 变量, 03: 事件, 04: 参变量  │
│    - 大多数数据只读，仅 DI 前缀 04 的参变量支持写入           │
│    - 值需通过乘法系数/加法系数换算                            │
│                                                              │
│  测点地址格式: "meter_address:DI_hex"                         │
│    示例: "000000001234:02010000"  (电表地址 + 数据标识)       │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

#### 4.4.3 libproto_iec104.so

```
┌──────────────────────────────────────────────────────────────┐
│                  libproto_iec104.so                       │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  导出符号:                                                    │
│    ems_plugin_info()     → {"IEC104", v1.0}                  │
│    ems_plugin_create()   → IEC104Handler 实例                │
│    ems_plugin_destroy()  → 释放实例                          │
│    ems_plugin_init()     → 初始化连接参数                     │
│    ems_plugin_connect()  → 建立 TCP 连接，启动总召唤          │
│    ems_plugin_read_batch() → 读取缓存测点值                  │
│    ems_plugin_write()    → 发送遥控/遥调命令                  │
│                                                              │
│  内部实现:                                                    │
│    IEC104ClientHandler - 作为控制站连接远动设备               │
│    IEC104ServerHandler - 作为被控站模拟设备                   │
│                                                              │
│  依赖: lib60870 (开源 IEC 60870-5-104 库)                    │
│                                                              │
│  配置参数 (JSON):                                             │
│    {                                                         │
│      "mode": "client|server",                                │
│      "ip": "192.168.1.30",                                   │
│      "port": 2404,                                           │
│      "common_address": 1,                                    │
│      "asdu_size": 0,                                         │
│      "timeout_ms": 5000,                                     │
│      "t1": 15, "t2": 10, "t3": 20,                          │
│      "k": 12, "w": 8                                        │
│    }                                                         │
│                                                              │
│  采集特点:                                                    │
│    - 事件驱动为主：设备主动上报遥测/遥信变化                   │
│    - 总召唤启动时全量同步，后续仅接收变化量                    │
│    - 遥控支持：单点/双点命令，选择-执行-撤销流程               │
│    - 品质描述符：OV/BL/SB/NT/IV 等品质位同步                 │
│    - ASDU 类型映射：M_ME_NC_1(短浮点)、M_SP_NA_1(单点)等    │
│                                                              │
│  测点地址格式: "common_address:io_address[:asdu_type]"        │
│    示例: "1:1001:M_ME_NC_1"  (公共地址1, IO地址1001, 短浮点) │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

#### 4.4.4 libproto_iec61850.so

```
┌──────────────────────────────────────────────────────────────┐
│                  libproto_iec61850.so                     │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  导出符号:                                                    │
│    ems_plugin_info()     → {"IEC61850", v1.0}                │
│    ems_plugin_create()   → IEC61850Handler 实例              │
│    ems_plugin_destroy()  → 释放实例                          │
│    ems_plugin_init()     → 初始化连接参数                     │
│    ems_plugin_connect()  → 建立 MMS 连接，发现数据模型        │
│    ems_plugin_read_batch() → 批量读取测点值                  │
│    ems_plugin_write()    → 写入控制命令                       │
│                                                              │
│  内部实现:                                                    │
│    IEC61850ClientHandler - MMS 客户端，连接 IED              │
│    IEC61850ServerHandler - MMS 服务端，模拟 IED              │
│    GooseManager         - GOOSE 订阅管理                     │
│                                                              │
│  依赖: libIEC61850 (开源 IEC 61850 MMS/GOOSE 库)             │
│                                                              │
│  配置参数 (JSON):                                             │
│    {                                                         │
│      "mode": "client|server",                                │
│      "ip": "192.168.1.40",                                   │
│      "port": 102,                                            │
│      "ied_name": "PCS001G",                                  │
│      "ld_name": "GenericLD",                                 │
│      "model_file": "",                                       │
│      "timeout_ms": 10000                                     │
│    }                                                         │
│                                                              │
│  采集特点:                                                    │
│    - 连接时自动发现数据模型（LN/DO/DA/FC）                    │
│    - 支持 DataSet 批量读取优化                                │
│    - 支持 GOOSE 组播订阅（二层报文）                          │
│    - 支持 ICD/SCL 模型文件加载                               │
│    - 连接过程分阶段：连接 → 发现模型 → 注册测点               │
│                                                              │
│  测点地址格式: "LD/LN.DO.DA" + FC                            │
│    示例: "MEAS/M0GGIO1.AnIn1.mag.f" (FC=MX)                 │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### 4.5 PluginManager 实现

```cpp
class PluginManager {
public:
    // 协议名 -> 动态库文件名映射
    static const std::unordered_map<std::string, std::string> PROTOCOL_LIB_MAP;

    // 按需加载协议插件
    ProtocolPlugin* load(const std::string& protocol_name);

    // 卸载指定协议（引用计数为0时真正 dlclose）
    void unload(const std::string& protocol_name);

    // 检查协议是否已加载
    bool is_loaded(const std::string& protocol_name) const;

private:
    struct PluginEntry {
        void* dl_handle{nullptr};              // dlopen 句柄
        ems_plugin_create_fn create_fn{nullptr};
        ems_plugin_destroy_fn destroy_fn{nullptr};
        ems_plugin_info_fn info_fn{nullptr};
        int ref_count{0};                      // 引用计数
    };

    std::unordered_map<std::string, PluginEntry> loaded_plugins_;
    std::string plugin_dir_{"./plugins"};      // 动态库搜索目录
};

// 协议名 -> 动态库文件名
const std::unordered_map<std::string, std::string>
PluginManager::PROTOCOL_LIB_MAP = {
    {"ModbusTCP",      "libproto_modbus.so"},
    {"ModbusRTU",      "libproto_modbus.so"},
    {"DLT645",         "libproto_dlt645.so"},
    {"IEC104",         "libproto_iec104.so"},
    {"IEC61850",       "libproto_iec61850.so"},
};
```

### 4.6 加载时序

```
时间 ──────────────────────────────────────────────────────────▶

ParaLoader          CollectEngine           PluginManager         .so 文件
    │                    │                       │                   │
    │ load_channels()    │                       │                   │
    │─── 通道列表 ──────▶│                       │                   │
    │                    │                       │                   │
    │                    │ for ch in channels:   │                   │
    │                    │  load(ch.protocol)    │                   │
    │                    │──────────────────────▶│                   │
    │                    │                       │                   │
    │                    │                       │ 已加载? ──No──▶ dlopen()
    │                    │                       │              dlsym()
    │                    │                       │◀─── 缓存 ───────│
    │                    │                       │                   │
    │                    │◀─── 插件实例 ─────────│                   │
    │                    │                       │                   │
    │                    │ 创建 ChannelThread    │                   │
    │                    │ 绑定插件实例          │                   │
    │                    │ 启动线程              │                   │
    │                    │                       │                   │
```

---

## 5. 采集流程

### 5.1 启动流程

```
1. 采集进程启动
   │
   ├── ParaLoader::load_all_as_config()
   │   ├── 加载设备列表 (DeviceEntity)
   │   ├── 加载通道列表 (ChannelEntity)
   │   └── 加载测点配置 (Yc/Yx/Yk/Yt)
   │
   ├── CollectEngine::initialize()
   │   ├── 初始化 DataDispatcher (连接 Redis)
   │   └── 初始化 PluginManager (设置插件目录)
   │
   └── CollectEngine::start_all()
       │
       └── for each channel:
           │
           ├── PluginManager::load(protocol_name)
           │   └── dlopen + dlsym (按需加载)
           │
           ├── 创建 ChannelContext
           │   ├── 绑定插件实例
           │   ├── 绑定设备/测点列表
           │   └── 设置采集周期
           │
           └── 启动 ChannelThread
               └── plugin->connect() → 进入采集循环
```

### 5.2 采集循环

```
┌──────────────────────────────────────────────────────────────┐
│                   通道线程采集循环                             │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  while (running_) {                                          │
│                                                              │
│      // 1. 检查连接状态                                      │
│      if (!plugin_->is_connected()) {                         │
│          reconnect();                                        │
│          continue;                                           │
│      }                                                      │
│                                                              │
│      // 2. 处理下发命令队列（遥控/遥调优先）                   │
│      while (!cmd_queue_.empty()) {                           │
│          auto cmd = cmd_queue_.pop();                        │
│          plugin_->write_value(cmd.point_code, cmd.value);    │
│      }                                                      │
│                                                              │
│      // 3. 批量读取遥测                                      │
│      for (auto& device : devices_) {                         │
│          auto yc_codes = device.get_yc_codes();              │
│          std::vector<PointValue> results;                    │
│          plugin_->read_batch(yc_codes, results);             │
│                                                              │
│          // 4. 批量读取遥信                                  │
│          auto yx_codes = device.get_yx_codes();              │
│          plugin_->read_batch(yx_codes, results);             │
│                                                              │
│          // 5. 数据分发                                      │
│          dispatcher_->update_points(results);                │
│      }                                                      │
│                                                              │
│      // 6. 等待下一周期                                      │
│      wait_for(poll_interval_ms_);                            │
│  }                                                          │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### 5.3 数据分发

采集到的数据通过 `DataDispatcher` 写入 RTDB 并通知其他进程：

```
采集线程 ──▶ DataDispatcher::update_points()
                 │
                 ├── RTDB::batch_update(points)        // 写入 Redis Hash
                 │
                 └── Redis::publish("rtdb.update", json) // Pub/Sub 通知
                                                      │
                                    ┌─────────────────┼─────────────────┐
                                    │                 │                 │
                               存储进程           策略进程          Web进程
                              (订阅写入TDengine)  (订阅触发策略)   (订阅推送前端)
```

---

## 6. 目录结构

```
backend/
├── src/
│   ├── net/                              # 网络采集模块
│   │   ├── collect/                      # 采集引擎
│   │   │   ├── CMakeLists.txt
│   │   │   ├── collect_engine.h/cpp      # 采集引擎主类
│   │   │   ├── channel_thread.h/cpp      # 通道线程
│   │   │   ├── data_dispatcher.h/cpp     # 数据分发
│   │   │   └── plugin_manager.h/cpp      # 插件管理器
│   │   │
│   │   ├── protocol/                     # 协议插件（各自编译为 .so）
│   │   │   ├── common/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── protocol_plugin_api.h # 插件标准 C 接口定义
│   │   │   │   └── protocol_handler.h    # 插件内部 C++ 抽象基类
│   │   │   │
│   │   │   ├── modbus/
│   │   │   │   ├── CMakeLists.txt        # → libproto_modbus.so
│   │   │   │   ├── modbus_handler.h/cpp  # Modbus 协议处理器
│   │   │   │   ├── modbus_client.h/cpp   # 客户端（TCP/RTU 统一）
│   │   │   │   └── modbus_server.h/cpp   # 服务端（TCP/RTU 统一）
│   │   │   │
│   │   │   ├── dlt645/
│   │   │   │   ├── CMakeLists.txt        # → libproto_dlt645.so
│   │   │   │   ├── dlt645_handler.h/cpp  # DLT645 协议处理器
│   │   │   │   ├── dlt645_client.h/cpp   # 客户端（TCP/RTU 统一）
│   │   │   │   ├── dlt645_server.h/cpp   # 服务端（TCP/RTU 统一）
│   │   │   │   └── dlt645_codec.h/cpp    # 编解码
│   │   │   │
│   │   │   ├── iec104/
│   │   │   │   ├── CMakeLists.txt        # → libproto_iec104.so
│   │   │   │   ├── iec104_handler.h/cpp  # IEC104 协议处理器
│   │   │   │   ├── iec104_client.h/cpp   # 客户端
│   │   │   │   ├── iec104_server.h/cpp   # 服务端
│   │   │   │   └── iec104_asdu_types.h/cpp # ASDU 类型编解码
│   │   │   │
│   │   │   └── iec61850/
│   │   │       ├── CMakeLists.txt        # → libproto_iec61850.so
│   │   │       ├── iec61850_handler.h/cpp
│   │   │       ├── iec61850_client.h/cpp # MMS 客户端
│   │   │       ├── iec61850_server.h/cpp # MMS 服务端
│   │   │       └── iec61850_goose.h/cpp  # GOOSE 管理
│   │   │
│   │   └── para_loader/                  # 参数加载模块
│   │
│   └── rtdb/                             # 实时数据库模块
│
├── plugins/                              # 运行时 .so 部署目录
│   ├── libproto_modbus.so
│   ├── libproto_dlt645.so
│   ├── libproto_iec104.so
│   └── libproto_iec61850.so
│
└── tests/
    └── net/
        └── protocol/
            ├── modbus_handler_test.cpp
            └── plugin_manager_test.cpp
```

---

## 7. CMake 构建配置

### 7.1 协议插件编译

每个协议插件编译为独立的共享库，使用 `MODULE` 类型（不被其他目标链接，仅运行时加载）：

```cmake
# net/protocol/modbus/CMakeLists.txt
add_library(proto_modbus MODULE
    modbus_handler.cpp
    modbus_client.cpp
    modbus_server.cpp
)

target_include_directories(proto_modbus PRIVATE
    ${CMAKE_SOURCE_DIR}/src/net/protocol/common
    ${CMAKE_SOURCE_DIR}/inc
)

target_link_libraries(proto_modbus PRIVATE
    libmodbus
)

set_target_properties(proto_modbus PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    PREFIX "lib"
)
```

### 7.2 采集引擎编译

```cmake
# net/collect/CMakeLists.txt
add_library(net_collect STATIC
    collect_engine.cpp
    channel_thread.cpp
    data_dispatcher.cpp
    plugin_manager.cpp
)

target_include_directories(net_collect PUBLIC
    ${CMAKE_SOURCE_DIR}/src/net/protocol/common
    ${CMAKE_SOURCE_DIR}/inc
)

target_link_libraries(net_collect PUBLIC
    ems_rtdb
    ems_net
    pthread
    dl
)
```

---

## 8. 关键设计决策

### 8.1 为什么选择一个通道一个线程？

| 方案 | 优点 | 缺点 |
|------|------|------|
| **每通道一线程** (选用) | 故障隔离好、实现简单、协议阻塞调用不影响其他通道 | 线程数与通道数成正比，大量通道时线程开销大 |
| 线程池 + 异步 I/O | 线程数可控、资源利用率高 | 协议库多为同步阻塞 API，需要复杂封装；故障隔离困难 |
| 协程 (C++20) | 轻量级、高并发 | 依赖 C++20、第三方协议库不支持协程、调试困难 |

对于 EMS 场景，通道数量通常在 10~100 范围，每通道一线程的方案在性能和复杂度之间取得了最佳平衡。

### 8.2 为什么选择动态库按需加载？

| 方案 | 优点 | 缺点 |
|------|------|------|
| **动态库按需加载** (选用) | 按需加载节省内存、新增协议无需重编译主程序、协议库可独立升级 | 运行时加载有微小开销、需要定义稳定的 C 接口 |
| 静态链接所有协议 | 无加载开销、调试方便 | 未使用的协议也占用内存、新增协议需重编译 |
| 进程级隔离 | 最强隔离、可独立重启 | 进程间通信开销大、架构复杂 |

动态库方案在灵活性和性能之间取得了平衡，且与 Python 版的 handler 架构理念一致——协议实现与核心引擎解耦。

### 8.3 为什么插件接口使用 C 导出函数？

- **ABI 稳定性**：C 接口没有 C++ 的名称修饰和 ABI 兼容性问题
- **跨编译器**：不同编译器编译的 `.so` 可以被同一主程序加载
- **简单可靠**：`dlopen`/`dlsym` 是 POSIX 标准，行为确定
- **内部仍用 C++**：插件内部使用 C++ 抽象类实现逻辑，仅边界使用 C 函数桥接

---

## 9. 错误处理与重连机制

### 9.1 连接异常处理

```
┌──────────────────────────────────────────────────────────────┐
│                   连接异常处理流程                             │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  采集循环中检测到连接断开:                                     │
│                                                              │
│  plugin_->is_connected() == false                            │
│       │                                                      │
│       ▼                                                      │
│  标记该通道所有测点品质为 Invalid                              │
│       │                                                      │
│       ▼                                                      │
│  进入重连循环:                                                │
│       │                                                      │
│       ├── 重试次数 < max_retries ?                            │
│       │       │                                              │
│       │    Yes│         No                                   │
│       │       ▼              ▼                               │
│       │  sleep(backoff)   标记通道为 Error                    │
│       │  connect()        通知上层（日志/告警）                │
│       │       │                                              │
│       │  成功? ──Yes──▶ 恢复采集循环                          │
│       │    │                                                  │
│       │   No ──▶ 继续重试                                     │
│       │                                                      │
│  指数退避策略:                                                │
│    backoff = min(base_delay * 2^retry_count, max_delay)      │
│    base_delay = 1s, max_delay = 60s                          │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### 9.2 采集超时处理

- 单次读取超时：不重连，仅标记该测点品质为 Invalid，继续采集其他测点
- 连续超时超过阈值：触发重连
- 遥控/遥调超时：返回执行失败，不自动重试（需人工确认）

---

## 10. 性能优化策略

### 10.1 Modbus 批量读取优化

参考 Python 版 `ModbusClientHandler.read_registers_batch_async` 的设计：

```
原始测点列表:
  slave=1, fc=3, addr=0   (FLOAT, 2寄存器)
  slave=1, fc=3, addr=2   (FLOAT, 2寄存器)
  slave=1, fc=3, addr=10  (UINT16, 1寄存器)
  slave=1, fc=3, addr=11  (UINT16, 1寄存器)
  slave=2, fc=3, addr=0   (FLOAT, 2寄存器)

优化后请求:
  1) slave=1, fc=3, read_holding_registers(0, 4)    // 合并 addr 0~3
  2) slave=1, fc=3, read_holding_registers(10, 2)   // 合并 addr 10~11
  3) slave=2, fc=3, read_holding_registers(0, 2)    // 不同 slave
```

### 10.2 IEC104 事件驱动优化

- 启动时发送总召唤（InterrogationCommand），全量同步
- 后续依赖设备主动上报（Spontaneous），仅处理变化量
- 设置合理的 T1/T2/T3 链路超时参数

### 10.3 IEC61850 DataSet 批量读取

参考 Python 版 `IEC61850ClientHandler.read_dataset_values`：

- 利用 DataSet 一次读取多个关联测点
- 按 FC (Functional Constraint) 分组读取
- 连接时自动发现数据模型，缓存 LN/DO 结构

---

## 11. 与 Python 版架构的映射关系

| Python 版 | C++ 版 | 说明 |
|-----------|--------|------|
| `ProtocolHandler` (ABC) | `ProtocolHandler` (C++ 抽象类) | 统一接口定义 |
| `ServerHandler` / `ClientHandler` | 插件内部实现 | 服务端/客户端模式由配置决定 |
| `ModbusServerHandler` | `libproto_modbus.so` 内部 | Modbus 协议实现 |
| `IEC104ServerHandler` | `libproto_iec104.so` 内部 | IEC104 协议实现 |
| `DLT645ServerHandler` | `libproto_dlt645.so` 内部 | DLT645 协议实现 |
| `IEC61850ServerHandler` | `libproto_iec61850.so` 内部 | IEC61850 协议实现 |
| `Device` (Facade) | `ChannelContext` + `DeviceContext` | 设备/通道管理 |
| `DataUpdateThread` | `ChannelThread` | 数据更新线程 |
| `PointManager` | `RTDB` + 测点列表 | 测点存储与管理 |
| `SimulationController` | 不在采集进程 | 模拟功能由模拟进程负责 |
| `import modbus_handler` | `dlopen(libproto_modbus.so)` | 按需加载 |
