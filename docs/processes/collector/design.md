# 采集执行与协议插件设计

> 2026-09-19 修订：本文说明 C++17 通信进程的采集执行子模块。进程完整职责见 [采集与上送进程](index.md)，设备/通道/协议的统一模型见 [设备、通道与协议](../../architecture/channel-protocol.md)。轮询线程与插件示例仅说明 acquisition 客户端路径，不代表所有协议或服务端角色都必须实现同一套轮询接口。

Redis 地址遵循 [Redis 键设计](../../common/realtime/redis-key-design.md)：全局唯一 unit_code 为根，设备与子设备 code 逐级展开；共享通道位于 station 命名空间，设备资产路径与通信路由分别解析。站字段统一使用 station_id/station_code。

公共实时状态访问采用 [C++17 多后端接口设计](../../common/realtime/access-design.md)：数据分发层注入 `RealtimeService/IRealtimeStore`，支持共享内存、Redis 与其他内存数据库派生类。本文涉及 Redis 最新值的流程和示意为默认部署实现；有界采样缓冲、事件通道、配置仓储与实时状态后端分别承担职责，协议和通道线程不判断后端类型。

设备类型、固有点位、厂家点表与实例绑定遵循 [EMS 设备模型与点表绑定设计](../../architecture/device-model.md)。采集按厂家路由读取并解码，再转换为标准逻辑点；控制按生效标准命令配置编码，执行前检查启用状态、版本、能力条件及独立反馈。

## 1. 概述

### 1.1 设计目标

数据采集模块是 EMS 系统的核心组件，负责通过多种工业通信协议与现场设备交互，完成遥测（YC）、遥信（YX）、遥控（YK）、遥调（YT）四类数据的采集与下发。

本设计基于以下核心原则：

- **资源单一所有者**：独立通道使用一个 C++17 工作线程；共享同一物理串口/总线的设备绑定由同一线程调度，禁止重复打开同一资源
- **设备与通道多对多**：支持采集专用、控制专用和采控共用，控制在事务边界插队，并保证采集新鲜度
- **规约动态库按需加载**：每种协议编译为独立的 `.so` 动态库，运行时按需 `dlopen` 加载，未使用的协议不占用内存
- **统一接口抽象**：所有协议插件实现统一的 C 接口，采集引擎不依赖任何具体协议实现

### 1.2 目标协议与采集能力

| 协议 | 传输方式 | 默认端口 | 典型设备 | 动态库文件名 |
|------|---------|---------|---------|-------------|
| Modbus TCP/RTU | TCP / RS485 | 502 | PCS、BMS、通用工控设备 | `libproto_modbus.so` |
| DL/T 645-2007 | 串口网关 / RS485 | 网关配置，无统一 TCP 端口 | 电能表 | `libproto_dlt645.so` |
| IEC 60870-5-104 | TCP | 2404 | 电力远动设备、保护装置 | `libproto_iec104.so` |
| IEC 61850 MMS | TCP | 102 | 变电站 IED，报告/控制按能力验收 | `libproto_iec61850.so` |

---

GOOSE/SV 不包含在 MMS 的 TCP 接口中，须独立 profile、发布/订阅角色与能力验收。上送服务端的模型、会话和控制入口见 [通道协议设计第 6 节](../../architecture/channel-protocol.md#6-上送通道设计)。

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
│  │   采集数据 ──▶ Redis Hash + Streams ──▶ 存储/策略                │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│                                                                         │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                    参数加载器 (ParaLoader)                        │   │
│  │                                                                 │   │
│  │   从 Redis 版本快照加载配置，缺失时经公共 ormpp API 读库         │   │
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
| **DataDispatcher** | 有界内存缓冲、经统一接口更新最新值、经独立事件接口发布采样；发布失败在预算内重试，超限报告缺口；默认使用 Redis/Streams，变更通知仅作刷新提示 |
| **ParaLoader** | 从存储发布的 Redis 快照或公共 ormpp API 加载指定版本的资产、通道绑定和测点配置，维护实际应用版本；数据库连接由采集进程独立管理 |

---

## 3. 通道线程模型

### 3.1 一个独立资源一个工作线程

独立通道对应一个 `std::thread`，线程独占协议实例；同一 RS485 总线的多个设备通过不同从站地址绑定到该通道。一个设备也可绑定多个独立通道，分别采集不同测点、控制或作为主备。

```text
PCS01 ── telemetry ──▶ 通道 A / 独立连接 A / 线程 A
       └─ control   ──▶ 通道 B / 独立连接 B / 线程 B

PCS02 ── both ───────▶ 通道 C / 共用连接 C / 线程 C
                       优先控制队列 + 到期采集队列

电表-01 / addr=1 ──┐
电表-02 / addr=2 ──┴──▶ 通道 D / 同一 RS485 / 线程 D
```

连接资源、设备资产归属、协议地址分别建模。多个逻辑通道若指向同一个不可并发物理资源，必须合并到同一调度器或拒绝冲突配置。详细调度见第 3.5、5.2 节。

### 3.2 线程隔离的好处

1. **阻塞隔离**：有界通信超时限制独立通道相互等待；共享总线仍会相互影响，插件内存错误仍可能导致整个采集进程退出
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

    std::vector<DeviceContext> devices;    // 由 device_channel_binding_version 构建的运行时设备视图
    int poll_interval_ms{1000};            // 采集周期

    std::mutex cmd_mutex;                  // 遥控/遥调命令队列锁
    std::condition_variable wakeup;        // 命令/停止/配置变更唤醒
    std::deque<CommandTask> cmd_queue;     // 简化示意，实际为有界优先队列
};
```

---

### 3.5 绑定、路由与控制插队

- `device_channel_binding_version` 保存 device_id、channel_id、config_version、operations 与类型化 device_address；采集专用/控制专用/采控共用对应 telemetry_in/control_out 的不同组合。本端 client/server 角色属于 channel_version.local_role，与此操作组合分开。vendor_point_route_version 保存厂家点的协议地址、编码与通道；point_binding 将标准逻辑点映射到厂家点并指定转换和主备来源。控制配置另行指定写入编码、启用条件和反馈逻辑点，写通道与反馈通道可以不同。
- telemetry 不允许控制写入；control 无常规遥测轮询，但允许协议确认/控制反馈读取；both 串行完成读取和控制。独立采控连接需设备支持，冲突控制仍按设备仲裁。
- 每次只开始一个有界协议事务，在响应完成或超时后检查高优先命令。不得打断已发送帧；大批量读取拆分为多个事务，避免整轮采集阻塞控制。
- ZeroMQ 命令入口只负责接收、校验并持久登记 inbox；执行器从持久 inbox 恢复后入队、唤醒，不在网络回调中直接调用协议句柄。线程等待必须可被命令唤醒；队列锁不得跨网络 I/O 持有。
- 设置控制连续预算、采集最大延迟和关键数据读取预算。优先队列满返回 queue_full；过期返回 expired。同优先级 FIFO，只有允许覆盖且未发送的设定值可合并，并记录 superseded。
- 命令由策略仲裁，通过公共 ormpp API 将命令与 outbox 同事务保存，再由 outbox 发布器用 ZeroMQ REQ/REP 交给采集的持久 inbox。采集执行前再次验证有效期、配置版本、控制权代次与互锁；通过公共 Repository 条件登记 MySQL 执行状态后执行并回读验证，回执与 outbox 同事务保存，重复/不确定结果按整体架构第 6 节处理。
- 通道故障与来源切换更新质量和 source_epoch；回读用发送后的新样本。未确认旧采集实例隔离时，不自动接管控制。

## 4. 动态库插件机制

### 4.1 设计思路

协议插件内部使用 C++17 纯虚类封装实现，通过版本化 C 导出函数提供接口。驱动工厂匹配 protocol_id + local_role + transport + plugin_abi_version；后续图中的单协议工厂为采集路径简化示意。每种协议编译为独立的 `.so` 文件，采集引擎通过 `dlopen`/`dlsym` 在运行时按需加载。

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

### 4.2 插件 C 接口契约

插件通过 C 导出符号和不透明句柄提供服务。接口属于目标设计，具体头文件及 ABI 版本在实现阶段按以下契约定义，并进行主程序与插件的兼容验证。

| 接口 | 契约 |
| --- | --- |
| `ems_plugin_info` | 返回 API 版本、协议名称、实现版本和能力；加载前检查版本及必需能力 |
| `ems_plugin_create / ems_plugin_destroy` | 按 protocol_id、local_role、transport 与 ABI 创建/释放通道实例；实例及插件资源在插件内释放 |
| `ems_plugin_init` | 接收通道连接配置、设备绑定、协议地址、测点路由及配置版本，拒绝歧义或冲突映射 |
| `ems_plugin_connect / ems_plugin_disconnect` | 采集客户端能力：管理主动连接，操作具有有界期限；服务端另提供 listener/session 生命周期能力 |
| `ems_plugin_read_batch` | 请求携带绑定/从站地址、测点路由和事务期限；返回类型化值、质量、时间和逐点结果，单次调用受事务预算限制 |
| `ems_plugin_write` | 请求携带 `command_id`、设备/绑定/测点标识、类型化值、配置版本、控制权代次和期限；返回执行阶段、协议确认及结构化错误 |
| `ems_plugin_is_connected / ems_plugin_last_error` | 查询连接状态和结构化错误信息，供通道状态与恢复逻辑使用 |

跨动态库边界使用固定宽度标量、指针/长度、不透明句柄或约定编码的缓冲区；字符串编码、缓冲区容量、实际长度、所有权和有效期须明确。STL 容器与 C++ 异常仅在插件内部使用。插件信息与工厂函数分别由 `ems_plugin_info_fn`、`ems_plugin_create_fn`、`ems_plugin_destroy_fn` 等函数指针类型承载。

### 4.3 插件内部 C++ 抽象

采集客户端角色可在 `.so` 内使用 `ProtocolHandler` 抽象类封装初始化、连接、批量读取、写入和错误查询。异步报告按事件接口实现；服务端角色独立实现 listener/session、读请求响应和上送映像，不强制继承轮询接口。内部请求与结果承载第 4.2 节的完整字段，值按布尔、整数、浮点、字符串等类型处理，并保留精度与质量信息。

阻塞采集实例由所属资源线程独占，异步实例由所属事件执行上下文串行管理；读取与写入通过绑定定位设备。每次调用在有界协议事务完成或超时后返回调度器；控制回读使用发送后的新样本，协议确认和设备反馈分别记录。C 导出适配层负责参数校验、编码转换、异常隔离与结果缓冲区管理。

### 4.4 各协议动态库实现概要

#### 4.4.1 libproto_modbus.so

```
┌──────────────────────────────────────────────────────────────┐
│                  libproto_modbus.so                       │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  导出符号:                                                    │
│    ems_plugin_info()     → {"ModbusTCP/RTU", API版本}           │
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
│    ems_plugin_info()     → {"DLT645", API版本}                  │
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
│    ems_plugin_info()     → {"IEC104", API版本}                  │
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
│    ems_plugin_info()     → {"IEC61850", API版本}                │
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
1. 采集进程启动（C++17）
   │
   ├── ParaLoader::load_all_as_config()
   │   ├── 读取 Redis 版本化快照；缺失时通过公共 ormpp API 读取 MySQL 指定版本
   │   ├── 校验资产、设备、通道及 device_channel_binding
   │   ├── 加载四遥点表与控制/回读路由
   │   └── 校验完整性后切换版本，反馈 actual_config_version
   │
   ├── CollectEngine::initialize()
   │   ├── 初始化 DataDispatcher (注入实时库与事件通道)
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

### 5.2 采集与控制循环

下列为调度伪代码，函数代表待实现职责，不能直接编译：

```text
while running:
    处理停止请求、配置切换和所有者状态
    清理已过期/失效命令并持久记录结果
    if 未连接:
        进行有界重连；可唤醒等待退避时间
        continue

    task = 在控制优先级、连续控制预算、采集新鲜度约束内选择任务
    if 没有任务:
        条件变量等待至下一采集/重连时刻，或被命令/停止/配置唤醒
        continue

    if task 是控制:
        再校验目标绑定、期限、配置版本、控制权和实时互锁
        查询/条件登记 command_id 的 MySQL 执行状态，失败不执行，拒绝不安全重复执行
        持久记录发送意图
        执行一个有超时的写事务，记录协议确认/失败/unknown
        调度发送后的反馈读取，满足条件才标 verified
    else:
        读取一个有界采集事务，生成时间、质量、来源代次和序号
        将结果交给有界分发队列

    事务结束返回调度点，重新检查控制队列
```

连接重试、写入和反馈读取均有时间上限。连续控制预算防止遥测饥饿；不能通过无限清空命令队列、整轮设备遍历或固定 sleep 实现上述要求。IEC104 等主动上报协议仍需处理接收/保活事件，不直接套用轮询专用循环。

### 5.3 数据分发与恢复

```text
通道线程 → DataDispatcher 有界内存队列（稳定 event_id）
                                  → 分别更新最新值、ZeroMQ 发布原始遥测
                                                            │
                                              存储消费原始事件 → TDengine
                                                            │
                                              失败计入可观察的历史缺口

PUB/SUB 本地接受不证明存储收到；内存样本不保证跨重启恢复

策略读取权威实时快照；告警/回执先持久写 MySQL/outbox
Drogon Web 读取快照并推送前端；变化提示与原始遥测使用独立 ZeroMQ PUB/SUB 主题
```

分发工作由独立工作线程执行，避免数据库/Redis 阻塞直接占用协议线程；队列满必须触发显式背压/降采样规则，不可无限增长。ZeroMQ 发布成功只表示本地接受；采集不等待归档确认。缓冲超限、进程重启、订阅离线或数据库故障可能丢样，应报告可观察的丢弃/缺口，不以最新值伪造缺失历史。

采集进程可通过 `backend/src/common/db/` 公共模块执行职责内的 MySQL 操作，数据库工作安排在配置/业务线程。公共模块封装 ormpp API；采集、存储、策略分别创建和维护自己的连接或连接池，不跨进程共享连接、事务或连接池实例，详见整体架构第 7.2 节。

最新值按 source_epoch/sequence 去旧；Redis 实时写入和 ZeroMQ 遥测发布分别记录结果。历史使用已接收到的原始事件，不能收到通知后再读 Hash。命令回执先与 outbox 同事务写入 MySQL，ZeroMQ 仅发出状态变化提示；采集按持久 inbox 与执行状态对账，不采用本地文件记录。

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

自研主工程统一 C++17：`CMAKE_CXX_STANDARD 17`、`CMAKE_CXX_STANDARD_REQUIRED ON`、`CMAKE_CXX_EXTENSIONS OFF`。以下为目标构建设计，实施时按选定工具链和第三方依赖版本验证，不以旧工程设置作为设计约束。

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

首期在目标硬件和实际通道数量下验证线程、内存及响应开销；通道规模扩大后可在 C++17 下评估异步 I/O/事件循环。无实测数据时不假定所有现场都在固定通道数量范围。

### 8.2 为什么选择动态库按需加载？

| 方案 | 优点 | 缺点 |
|------|------|------|
| **动态库按需加载** (选用) | 按需加载节省内存、新增协议无需重编译主程序、协议库可独立升级 | 运行时加载有微小开销、需要定义稳定的 C 接口 |
| 静态链接所有协议 | 无加载开销、调试方便 | 未使用的协议也占用内存、新增协议需重编译 |
| 进程级隔离 | 最强隔离、可独立重启 | 进程间通信开销大、架构复杂 |

动态库方案支持按需加载，使协议实现与核心引擎解耦。

### 8.3 为什么插件接口使用 C 导出函数？

- **ABI 约束**：C 导出避免名称修饰，但仍需统一结构体布局、版本、调用约定和内存所有权；不跨边界传递 STL 或异常
- **兼容验证**：不同编译器/运行库组合需实测，不能仅凭 extern C 保证二进制兼容
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
│       │  可唤醒退避等待   标记通道为 Error                    │
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

- 单次读取超时：标记该测点品质为 Invalid，按协议清理/隔离迟到响应，必要时重建会话后再采下一事务
- 连续超时超过阈值：触发重连
- 遥控/遥调超时：发送前到期为 expired；发送后不能确认结果为 unknown，不能直接认定设备执行失败；先回读/核验，不盲目自动重发

---

## 10. 性能优化策略

### 10.1 Modbus 批量读取优化

将同一从站、同一功能码的连续寄存器合并读取：

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

上述 Modbus 合并仍受单事务长度和控制延迟预算限制；每个合并请求完成后必须返回调度点检查控制队列。

### 10.2 IEC104 事件驱动优化

- 启动时发送总召唤（InterrogationCommand），全量同步
- 后续依赖设备主动上报（Spontaneous），仅处理变化量
- 设置合理的 T1/T2/T3 链路超时参数

### 10.3 IEC61850 DataSet 批量读取

按数据模型组织批量读取：

- 利用 DataSet 一次读取多个关联测点
- 按 FC (Functional Constraint) 分组读取
- 连接时自动发现数据模型，缓存 LN/DO 结构
