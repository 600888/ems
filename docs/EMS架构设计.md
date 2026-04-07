# EMS系统架构设计文档

## 1. 系统概述

### 1.1 项目背景

EMS（Energy Management System，能源管理系统）是一个多进程架构的工业能源管理系统，负责对能源数据进行采集、存储、分析和可视化展示。系统采用多进程架构设计，通过进程间通信实现各模块的解耦和协同工作。

### 1.2 系统目标

- 实现多种工业通讯协议的统一数据采集
- 提供高实时性的数据访问能力
- 支持时序数据的长期存储和分析
- 提供友好的Web界面和API服务
- 支持灵活的业务策略配置和执行

### 1.3 系统特点

- **多进程架构**：各功能模块独立运行，通过消息队列实现进程间通信
- **高实时性**：基于Redis实现毫秒级的数据读写响应
- **可扩展性**：模块化设计支持灵活的功能扩展
- **高可靠性**：进程隔离，单点故障不影响整体系统运行

## 2. 系统架构

### 2.1 整体架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                         EMS系统架构                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌───────┐│
│  │  Web服务进程  │  │  策略进程     │  │  存储进程     │  │采集进程││
│  │              │  │              │  │              │  │       ││
│  │  - HTTP API  │  │  - 策略引擎  │  │  - TDengine │  │- Modbus│
│  │  - WebSocket │  │  - 告警管理  │  │  - 数据写入  │  │- IEC104│
│  │  - 用户认证  │  │  - 控制指令  │  │  - 数据查询  │  │- DLT645│
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘  └───┬───┘│
│         │                │                 │              │    │
│         └────────────────┴────────┬────────┴──────────────┘    │
│                                   │                           │
│                          ┌────────▼────────┐                   │
│                          │  Redis实时数据库 │                   │
│                          │                  │                   │
│                          │  - 实时数据缓存   │                   │
│                          │  - 进程间消息队列 │                   │
│                          │  - 设备状态管理   │                   │
│                          └─────────────────┘                   │
│                                   │                           │
│                          ┌────────▼────────┐                   │
│                          │ TDengine时序数据库│                   │
│                          │                  │                   │
│                          │  - 历史数据存储   │                   │
│                          │  - 数据聚合分析   │                   │
│                          │  - 趋势预测      │                   │
│                          └─────────────────┘                   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 架构分层

EMS系统采用分层架构设计，从下到上分为五个层次：

1. **设备接入层**：负责与各种工业设备通信，采集数据
2. **数据采集层**：统一管理各协议的数据采集任务
3. **数据处理层**：负责数据的转换、清洗和分发
4. **数据存储层**：提供实时数据和历史数据的存储能力
5. **应用服务层**：提供Web界面和API服务，支持业务策略执行

### 2.3 进程架构

EMS系统由四个核心进程组成，各进程职责明确，通过Redis实现进程间通信。

```
┌─────────────────────────────────────────────────────────────┐
│                      进程间通信架构                            │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌────────────┐                              ┌────────────┐  │
│  │  采集进程   │ ───────────────────────────▶│  Redis     │  │
│  │            │     发布实时数据              │ Pub/Sub    │  │
│  │ - 协议适配  │                              │            │  │
│  │ - 数据采集  │                              │ Channels:  │  │
│  │ - 设备管理  │                              │ - rtdb.*   │  │
│  └────────────┘                              │ - alert.*  │  │
│                                              │ - cmd.*    │  │
│  ┌────────────┐                              │            │  │
│  │  存储进程   │ ───────────────────────────▶│            │  │
│  │            │     订阅实时数据              │            │  │
│  │ - TDengine │     写入历史数据             └────────────┘  │
│  │ - 数据聚合  │                              ▲              │
│  │ - 数据查询  │                              │              │
│  └────────────┘                              │              │
│                                              │              │
│  ┌────────────┐                              │              │
│  │  策略进程   │ ───────────────────────────▶│              │
│  │            │     订阅实时数据              │              │
│  │ - 策略引擎  │     发布控制指令             │              │
│  │ - 告警管理  │                              │              │
│  │ - 联动控制  │                              │              │
│  └────────────┘                              │              │
│                                              │              │
│  ┌────────────┐                              │              │
│  │ Web服务进程│ ────────────────────────────│              │
│  │            │     订阅实时数据              │              │
│  │ - HTTP API │     读取实时数据             │              │
│  │ - Web UI   │     发送控制指令             │              │
│  │ - WebSocket│                              │              │
│  └────────────┘                              │              │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

## 3. 核心进程设计

### 3.1 数据采集进程

#### 3.1.1 进程职责

数据采集进程负责与各种工业设备建立连接，按照约定的通讯协议进行数据采集和下发控制指令。

#### 3.1.2 支持的通讯协议

| 协议名称 | 协议类型 | 应用场景 | 传输层 |
|---------|---------|---------|-------|
| Modbus RTU/TCP | 串口/网络 | 通用工业设备 | TCP/RTU |
| IEC 60870-5-104 | 网络协议 | 电力系统远动 | TCP |
| DLT 645-2007 | 串口协议 | 电能表通信 | RS485 |
| IEC 61850 | 网络协议 | 变电站自动化 | MMS/SMV |

#### 3.1.3 协议适配器设计

```
┌──────────────────────────────────────────────────────┐
│                  协议适配器架构                        │
├──────────────────────────────────────────────────────┤
│                                                      │
│  ┌──────────────────────────────────────────────┐   │
│  │              协议管理器                        │   │
│  │  - 协议插件加载                                │   │
│  │  - 连接池管理                                  │   │
│  │  - 任务调度                                    │   │
│  └──────────────────────────────────────────────┘   │
│           │           │           │           │      │
│           ▼           ▼           ▼           ▼      │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐  │
│  │ Modbus   │ │ IEC104   │ │ DLT645   │ │ IEC61850│  │
│  │ Adapter  │ │ Adapter  │ │ Adapter  │ │ Adapter │  │
│  │          │ │          │ │          │ │         │  │
│  │ - RTU    │ │ - ASDU   │ │ - 645-97 │ │ - MMS   │  │
│  │ - TCP    │ │ - 链路   │ │ - 645-07 │ │ - GOOSE │  │
│  │          │ │   管理   │ │          │ │ - SMV   │  │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘  │
│                                                      │
└──────────────────────────────────────────────────────┘
```

#### 3.1.4 采集策略

- **轮询采集**：按配置的周期主动查询设备数据
- **变化采集**：仅在数据变化时上报（适用于支持变化检测的协议）
- **事件驱动**：设备主动上报告警或事件
- **批量采集**：优化网络传输，减少通信次数

### 3.2 存储进程

#### 3.2.1 进程职责

存储进程负责将采集的实时数据写入TDengine时序数据库，并提供数据查询接口。

#### 3.2.2 数据写入流程

```
设备数据 ──▶ 采集进程 ──▶ Redis Pub/Sub ──▶ 存储进程 ──▶ TDengine
                                │
                                └──▶ 实时数据缓存（Redis Hash）
                                                      │
                                                      ▼
                                              历史数据表（TDengine）
                                                      │
                                                      ├──▶ 原始数据表
                                                      ├──▶ 分钟聚合表
                                                      ├──▶ 小时聚合表
                                                      └──▶ 日聚合表
```

#### 3.2.3 TDengine数据库设计

##### 超级表设计

```sql
-- 测点超级表
CREATE STABLE IF NOT EXISTS points (
    ts TIMESTAMP,
    value DOUBLE,
    quality INT
) TAGS (
    device_id INT,
    point_id INT,
    point_type BINARY(32),
    point_name BINARY(64)
);

-- 告警超级表
CREATE STABLE IF NOT EXISTS alerts (
    ts TIMESTAMP,
    level INT,
    message BINARY(256),
    point_id INT
) TAGS (
    device_id INT,
    alert_type BINARY(32)
);

-- 事件超级表
CREATE STABLE IF NOT EXISTS events (
    ts TIMESTAMP,
    event_type INT,
    event_data BINARY(512),
    operator BINARY(64)
) TAGS (
    device_id INT,
    source BINARY(32)
);
```

##### 子表创建规则

```sql
-- 按设备创建子表
CREATE TABLE device_001 USING points TAGS (1, 0, 'power', 'Voltage_A');

-- 按测点类型创建子表
CREATE TABLE power_points USING points TAGS (1, 1, 'power', '');
CREATE TABLE energy_points USING points TAGS (1, 2, 'energy', '');
```

### 3.3 策略进程

#### 3.3.1 进程职责

策略进程负责执行能源管理策略，包括告警监测、联动控制、负荷预测等业务逻辑。

#### 3.3.2 核心功能模块

```
┌─────────────────────────────────────────────────────┐
│                   策略进程架构                        │
├─────────────────────────────────────────────────────┤
│                                                     │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐ │
│  │  策略引擎    │  │  告警管理    │  │  联动控制    │ │
│  │             │  │             │  │             │ │
│  │ - 策略加载   │  │ - 告警生成   │  │ - 条件判断   │ │
│  │ - 策略执行   │  │ - 告警升级   │  │ - 动作执行   │ │
│  │ - 策略调度   │  │ - 告警确认   │  │ - 延时处理   │ │
│  │             │  │ - 告警恢复   │  │ - 互锁保护   │ │
│  └─────────────┘  └─────────────┘  └─────────────┘ │
│                                                     │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐ │
│  │  数据分析    │  │  报表生成    │  │  指令下发    │ │
│  │             │  │             │  │             │ │
│  │ - 趋势分析   │  │ - 统计报表   │  │ - 指令验证   │ │
│  │ - 异常检测   │  │ - 能流分析   │  │ - 指令下发   │ │
│  │ - 负荷预测   │  │ - 对标分析   │  │ - 结果反馈   │ │
│  └─────────────┘  └─────────────┘  └─────────────┘ │
│                                                     │
└─────────────────────────────────────────────────────┘
```

#### 3.3.3 策略定义示例

策略采用JSON格式定义，支持灵活配置：

```json
{
  "strategy_id": "strategy_001",
  "name": "电压越限告警",
  "trigger": {
    "type": "data_change",
    "condition": "voltage > 240 OR voltage < 190"
  },
  "actions": [
    {
      "type": "alert",
      "level": "warning",
      "message": "电压越限，当前值: ${voltage}V"
    },
    {
      "type": "control",
      "device": "transformer_001",
      "command": "adjust_tap",
      "params": {
        "direction": "auto"
      }
    }
  ],
  "cooldown": 300
}
```

### 3.4 Web服务进程

#### 3.4.1 进程职责

Web服务进程提供用户交互界面和API服务，是系统的对外窗口。

#### 3.4.2 服务架构

```
┌─────────────────────────────────────────────────────┐
│                  Web服务进程架构                      │
├─────────────────────────────────────────────────────┤
│                                                     │
│                    HTTP Server                      │
│                       (Nginx/Gunicorn)              │
│                           │                         │
│              ┌────────────┼────────────┐           │
│              ▼            ▼            ▼           │
│       ┌──────────┐ ┌──────────┐ ┌──────────┐       │
│       │  REST API │ │WebSocket │ │   Admin  │       │
│       │           │ │  Server  │ │  Portal  │       │
│       └──────────┘ └──────────┘ └──────────┘       │
│              │            │            │           │
│              └────────────┴────────────┘           │
│                           │                         │
│              ┌────────────┼────────────┐           │
│              ▼            ▼            ▼           │
│       ┌──────────┐ ┌──────────┐ ┌──────────┐       │
│       │  数据服务 │ │  设备服务 │ │  策略服务 │       │
│       └──────────┘ └──────────┘ └──────────┘       │
│                                                     │
└─────────────────────────────────────────────────────┘
```

#### 3.4.3 API接口设计

##### 实时数据接口

```
GET  /api/v1/realtime/{device_id}/{point_id}
GET  /api/v1/realtime/batch
POST /api/v1/realtime/subscribe
POST /api/v1/realtime/unsubscribe
```

##### 历史数据接口

```
GET  /api/v1/history/{device_id}/{point_id}
     ?start_time=xxx&end_time=xxx&interval=xxx&aggregation=xxx
GET  /api/v1/history/aggregate
     ?device_ids=xxx&point_type=xxx&time_range=xxx&granularity=xxx
```

##### 设备管理接口

```
GET    /api/v1/devices
GET    /api/v1/devices/{device_id}
POST   /api/v1/devices
PUT    /api/v1/devices/{device_id}
DELETE /api/v1/devices/{device_id}
POST   /api/v1/devices/{device_id}/connect
POST   /api/v1/devices/{device_id}/disconnect
```

##### 控制指令接口

```
POST   /api/v1/control/{device_id}/{point_id}
       {
         "command": "set_value",
         "value": 220,
         "quality": "command"
       }
GET    /api/v1/control/history
```

## 4. 数据存储架构

### 4.1 Redis实时数据库设计

#### 4.1.1 数据结构设计

```
┌──────────────────────────────────────────────────────┐
│                  Redis数据结构设计                      │
├──────────────────────────────────────────────────────┤
│                                                      │
│  Hash: 实时数据缓存                                    │
│  ──────────────────────────────                      │
│  Key: rtdb:device:{device_id}:point:{point_id}       │
│  Field: value, quality, timestamp                    │
│  Example: rtdb:device:001:point:001                  │
│           value: "220.5"                             │
│           quality: "good"                            │
│           timestamp: "1709289600"                    │
│                                                      │
│  ─────────────────────────────────────────────────   │
│                                                      │
│  Hash: 设备状态                                        │
│  ──────────────────────────────                      │
│  Key: rtdb:device:{device_id}:status                 │
│  Field: online, last_update, protocol, ip, port       │
│                                                      │
│  ─────────────────────────────────────────────────   │
│                                                      │
│  Pub/Sub: 消息通道                                     │
│  ──────────────────────────────                      │
│  Channel: rtdb:data        - 实时数据更新             │
│  Channel: rtdb:alert       - 告警事件                 │
│  Channel: rtdb:command     - 控制指令                 │
│  Channel: rtdb:device:*    - 设备状态变更             │
│                                                      │
│  ─────────────────────────────────────────────────   │
│                                                      │
│  Sorted Set: 最新告警列表                              │
│  ──────────────────────────────                      │
│  Key: alert:unresolved                               │
│  Score: timestamp                                    │
│  Member: alert_id                                    │
│                                                      │
│  ─────────────────────────────────────────────────   │
│                                                      │
│  List: 控制指令队列                                    │
│  ──────────────────────────────                      │
│  Key: command:pending                                │
│  Value: JSON格式的控制指令                            │
│                                                      │
└──────────────────────────────────────────────────────┘
```

#### 4.1.2 数据过期策略

- 实时数据缓存：保留最近1小时数据，超时自动删除
- 设备状态：实时更新，断线超过5分钟标记为离线
- 告警数据：未处理告警永久保留，已处理告警保留30天
- 控制指令：执行完成后保留24小时

### 4.2 TDengine时序数据库设计

#### 4.2.1 数据库schema

```sql
-- 创建数据库
CREATE DATABASE IF NOT EXISTS ems
    KEEP 365          -- 数据保留365天
    DAYS 10           -- 文件块时间跨度10天
    BUFFER 256        -- 写入缓冲区256MB
    CACHEMODEL 'last' -- 缓存策略：保留最新数据
    CACHESIZE 128     -- 缓存大小128MB
    WAL_LEVEL 2       -- WAL持久化级别
    VGROUPS 4         -- 虚拟节点数
    REPLICA 1;        -- 副本数
```

#### 4.2.2 表结构设计

```sql
-- 测点数据超级表
CREATE STABLE IF NOT EXISTS ems.points (
    ts TIMESTAMP,
    value DOUBLE,
    quality INT,
    raw_data BINARY(128)
) TAGS (
    device_id INT,
    point_id INT,
    point_code BINARY(32),
    point_name BINARY(64),
    unit BINARY(16),
    point_type BINARY(16)
);

-- 告警事件超级表
CREATE STABLE IF NOT EXISTS ems.alerts (
    ts TIMESTAMP,
    level INT,
    category BINARY(32),
    message BINARY(256),
    point_id INT,
    value DOUBLE,
    threshold DOUBLE
) TAGS (
    device_id INT,
    alert_type BINARY(32),
    strategy_id BINARY(32)
);

-- 控制指令记录表
CREATE TABLE IF NOT EXISTS ems.control_logs (
    ts TIMESTAMP,
    device_id INT,
    point_id INT,
    command BINARY(32),
    value DOUBLE,
    result INT,
    message BINARY(128),
    operator BINARY(64)
);

-- 设备状态记录表
CREATE TABLE IF NOT EXISTS ems.device_status (
    ts TIMESTAMP,
    device_id INT,
    status INT,
    response_time INT,
    error_count INT
);
```

#### 4.2.3 数据聚合设计

为提高查询效率，系统自动创建多个时间粒度的聚合表：

```sql
-- 分钟聚合表
CREATE TABLE IF NOT EXISTS ems.points_1m AS 
SELECT _start, AVG(value), MAX(value), MIN(value), STDDEV(value), COUNT(*)
FROM ems.points INTERVAL(1m);

-- 小时聚合表
CREATE TABLE IF NOT EXISTS ems.points_1h AS 
SELECT _start, AVG(value), MAX(value), MIN(value), STDDEV(value), COUNT(*)
FROM ems.points INTERVAL(1h);

-- 日聚合表
CREATE TABLE IF NOT EXISTS ems.points_1d AS 
SELECT _start, AVG(value), MAX(value), MIN(value), SUM(value), COUNT(*)
FROM ems.points INTERVAL(1d);
```

## 5. 进程间通信机制

### 5.1 通信模式

EMS系统采用多种Redis通信模式实现进程间解耦：

#### 5.1.1 Pub/Sub模式

用于实时数据分发和事件通知：

```
采集进程 ──发布──▶ rtdb:data ──订阅──▶ 存储进程
                                    ──订阅──▶ 策略进程
                                    ──订阅──▶ Web服务进程
```

#### 5.1.2 Stream模式

用于可靠的消息传递（控制指令）：

```
Web服务进程 ──XADD──▶ command:stream ──XREADGROUP──▶ 采集进程
```

#### 5.1.3 Hash操作模式

用于实时数据读写：

```
Web服务进程 ──HGETALL──▶ rtdb:device:001:status
                      ──HSET──▶ rtdb:device:001:point:001
```

### 5.2 消息格式设计

#### 5.2.1 实时数据消息

```json
{
  "type": "data_update",
  "timestamp": 1709289600,
  "device_id": "001",
  "device_code": "transformer_001",
  "data": [
    {
      "point_id": "001",
      "point_code": "voltage_a",
      "value": 220.5,
      "quality": "good",
      "timestamp": 1709289600
    },
    {
      "point_id": "002",
      "point_code": "current_a",
      "value": 45.2,
      "quality": "good",
      "timestamp": 1709289600
    }
  ]
}
```

#### 5.2.2 告警消息

```json
{
  "type": "alert",
  "timestamp": 1709289600,
  "alert_id": "alert_20240101_001",
  "level": 2,
  "category": "over_limit",
  "device_id": "001",
  "point_id": "001",
  "message": "电压越上限，当前值: 250V，阈值: 240V",
  "value": 250.0,
  "threshold": 240.0,
  "strategy_id": "strategy_001"
}
```

#### 5.2.3 控制指令消息

```json
{
  "type": "command",
  "timestamp": 1709289600,
  "command_id": "cmd_20240101_001",
  "device_id": "001",
  "point_id": "001",
  "command": "set_value",
  "value": 220,
  "priority": 1,
  "timeout": 30,
  "operator": "admin",
  "status": "pending"
}
```

## 6. 通讯协议详细设计

### 6.1 Modbus协议适配器

#### 6.1.1 支持的通信模式

- **Modbus RTU**：通过串口通信，支持RS232/RS485
- **Modbus TCP**：通过TCP/IP网络通信
- **Modbus ASCII**：通过串口通信（较少使用）

#### 6.1.2 功能码支持

| 功能码 | 名称 | 说明 |
|-------|------|------|
| 0x01 | Read Coils | 读取线圈状态 |
| 0x02 | Read Discrete Inputs | 读取离散输入 |
| 0x03 | Read Holding Registers | 读取保持寄存器 |
| 0x04 | Read Input Registers | 读取输入寄存器 |
| 0x05 | Write Single Coil | 写入单个线圈 |
| 0x06 | Write Single Register | 写入单个寄存器 |
| 0x0F | Write Multiple Coils | 写入多个线圈 |
| 0x10 | Write Multiple Registers | 写入多个寄存器 |

#### 6.1.3 配置示例

```json
{
  "protocol": "modbus_tcp",
  "device_id": "modbus_device_001",
  "connection": {
    "host": "192.168.1.100",
    "port": 502,
    "timeout": 5000,
    "retry": 3
  },
  "mapping": [
    {
      "point_id": "001",
      "point_code": "voltage_a",
      "function_code": 4,
      "address": 0,
      "data_type": "float",
      "register_count": 2,
      "scale": 0.1,
      "unit": "V"
    },
    {
      "point_id": "002",
      "point_code": "power",
      "function_code": 4,
      "address": 10,
      "data_type": "int32",
      "register_count": 2,
      "scale": 1,
      "unit": "kW"
    }
  ],
  "polling_interval": 1000
}
```

### 6.2 IEC 60870-5-104协议适配器

#### 6.2.1 协议概述

IEC 60870-5-104是电力系统远动通信标准，用于远程控制和数据采集。

#### 6.2.2 ASDU类型支持

**监视方向（从站到主站）**：

| 类型标识 | 名称 | 说明 |
|---------|------|------|
| M_SP_TA_1 | 单点信息 | 带时标的单点状态 |
| M_DP_TA_1 | 双点信息 | 带时标的双点状态 |
| M_ME_TA_1 | 测量值 | 带时标的标度化值 |
| M_ME_TD_1 | 测量值 | 带时标和品质描述符的标度化值 |
| M_IT_TA_1 | 累计量 | 带时标的累计量 |

**控制方向（主站到从站）**：

| 类型标识 | 名称 | 说明 |
|---------|------|------|
| C_SC_TA_1 | 单点命令 | 带时标的单点控制命令 |
| C_DC_TA_1 | 双点命令 | 带时标的双点控制命令 |
| C_SE_TA_1 | 调节命令 | 带时标的调节命令 |

#### 6.2.3 配置示例

```json
{
  "protocol": "iec104",
  "device_id": "iec104_device_001",
  "connection": {
    "host": "192.168.1.101",
    "port": 2404,
    "timeout": 15000,
    "redundancy": {
      "enabled": true,
      "peers": ["192.168.1.102"]
    }
  },
  "common_address": 1,
  "information_objects": [
    {
      "ioa": 1,
      "point_id": "001",
      "point_code": "breaker_status",
      "type_id": 1,
      "description": "1号断路器状态"
    },
    {
      "ioa": 10,
      "point_id": "002",
      "point_code": "voltage_35kv",
      "type_id": 13,
      "description": "35kV母线电压"
    },
    {
      "ioa": 20,
      "point_id": "003",
      "point_code": "active_power",
      "type_id": 13,
      "description": "有功功率"
    }
  ],
  "interrogation": {
    "general": 60000,
    "station": 300000
  }
}
```

### 6.3 DLT 645-2007协议适配器

#### 6.3.1 协议概述

DLT 645-2007是中国电能表通信协议标准，广泛应用于电力系统的电能数据采集。

#### 6.3.2 数据标识

| 数据标识 | 名称 | 说明 |
|---------|------|------|
| 9010 | 当前正向有功总电能 | 累计用电量 |
| 9020 | 当前正向无功总电能 | 累计无功电量 |
| 0000 | 电压 | A/B/C三相电压 |
| 0001 | 电流 | A/B/C三相电流 |
| 0002 | 有功功率 | 总及分相有功功率 |
| 0003 | 无功功率 | 总及分相无功功率 |
| 0004 | 功率因数 | 总及分相功率因数 |
| 0005 | 频率 | 电网频率 |

#### 6.3.3 配置示例

```json
{
  "protocol": "dlt645_2007",
  "device_id": "meter_001",
  "connection": {
    "type": "serial",
    "port": "/dev/ttyUSB0",
    "baudrate": 2400,
    "databits": 8,
    "stopbits": 1,
    "parity": "E",
    "timeout": 5000
  },
  "device_address": "123456789012",
  "data_items": [
    {
      "data_id": "9010",
      "point_id": "001",
      "point_code": "active_energy",
      "description": "正向有功总电能",
      "unit": "kWh",
      "data_type": "bcd",
      "length": 4
    },
    {
      "data_id": "0000",
      "point_id": "002",
      "point_code": "voltage",
      "description": "电压",
      "unit": "V",
      "data_type": "bcd",
      "length": 3,
      "phases": ["A", "B", "C"]
    },
    {
      "data_id": "0001",
      "point_id": "003",
      "point_code": "current",
      "description": "电流",
      "unit": "A",
      "data_type": "bcd",
      "length": 3,
      "phases": ["A", "B", "C"]
    }
  ],
  "polling_interval": 60000
}
```

### 6.4 IEC 61850协议适配器

#### 6.4.1 协议概述

IEC 61850是变电站通信网络和系统标准，支持多种服务类型。

#### 6.4.2 服务类型支持

| 服务类型 | 说明 | 典型应用 |
|---------|------|---------|
| MMS | 制造报文规范 | 遥信、遥测、遥控 |
| GOOSE | 通用对象面向变电站事件 | 快速状态传输 |
| SMV | 采样测量值 | 合并单元数据传输 |

#### 6.4.3 配置示例

```json
{
  "protocol": "iec61850",
  "device_id": "ied_001",
  "connection": {
    "host": "192.168.1.110",
    "mms_port": 102,
    "goose_iface": "eth0",
    "timeout": 10000
  },
  "ied_name": "IED1",
  "data_sets": [
    {
      "name": "Measurements",
      "fcda": [
        {
          "ld_inst": "LD0",
          "prefix": "MMXU",
          "ln_class": "MX",
          "ln_inst": "1",
          "do_name": "Hz",
          "point_id": "001",
          "description": "频率"
        },
        {
          "ld_inst": "LD0",
          "prefix": "MMXU",
          "ln_class": "MX",
          "ln_inst": "1",
          "do_name": "TotW",
          "point_id": "002",
          "description": "总有功功率"
        }
      ]
    },
    {
      "name": "Status",
      "fcda": [
        {
          "ld_inst": "LD0",
          "prefix": "XCBR",
          "ln_class": "ST",
          "ln_inst": "1",
          "do_name": "Pos",
          "point_id": "101",
          "description": "断路器位置"
        }
      ]
    }
  ],
  "reporting": {
    "enabled": true,
    "rpt_id": "LLN0.RPT",
    "trg_opt": ["dchg", "qchg", "period"],
    "period": 1000
  }
}
```

## 7. 系统部署架构

### 7.1 单机部署架构

```
┌─────────────────────────────────────────────────────┐
│                   单机部署架构                        │
├─────────────────────────────────────────────────────┤
│                                                     │
│   ┌────────────────────────────────────────────┐   │
│   │              操作系统                        │   │
│   │  ┌──────────────────────────────────────┐  │   │
│   │  │         Docker容器                    │  │   │
│   │  │  ┌────────┐ ┌────────┐ ┌────────┐   │  │   │
│   │  │  │采集进程 │ │存储进程 │ │策略进程 │   │  │   │
│   │  │  └────────┘ └────────┘ └────────┘   │  │   │
│   │  │  ┌────────────────────────────┐    │  │   │
│   │  │  │       Web服务进程          │    │  │   │
│   │  │  └────────────────────────────┘    │  │   │
│   │  └──────────────────────────────────────┘  │   │
│   │  ┌────────────┐  ┌───────────────────┐   │   │
│   │  │   Redis    │  │    TDengine       │   │   │
│   │  │   (容器)   │  │    (容器)          │   │   │
│   │  └────────────┘  └───────────────────┘   │   │
│   └────────────────────────────────────────────┘   │
│                                                     │
└─────────────────────────────────────────────────────┘
```

### 7.2 分布式部署架构

```
┌─────────────────────────────────────────────────────────────┐
│                    分布式部署架构                             │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────────────────────────────────────────────┐  │
│  │                    负载均衡层                          │  │
│  │                    Nginx / HAProxy                    │  │
│  └──────────────────────────────────────────────────────┘  │
│                              │                              │
│                              ▼                              │
│  ┌──────────────────────────────────────────────────────┐  │
│  │                  Web服务集群                           │  │
│  │  ┌────────────┐  ┌────────────┐  ┌────────────┐    │  │
│  │  │ Web进程 #1  │  │ Web进程 #2  │  │ Web进程 #N  │    │  │
│  │  └────────────┘  └────────────┘  └────────────┘    │  │
│  └──────────────────────────────────────────────────────┘  │
│                    │                  │                     │
│                    ▼                  ▼                     │
│  ┌──────────────────────┐  ┌──────────────────────┐        │
│  │      Redis集群        │  │   TDengine集群      │        │
│  │  ┌────┐ ┌────┐ ┌────┐ │  │  ┌────┐ ┌────┐ ┌────┐│       │
│  │  │Master│ │Slave│ │Slave│ │  │  qnode │  qnode │  qnode ││       │
│  │  └────┘ └────┘ └────┘ │  │  └────┘ └────┘ └────┘│       │
│  └──────────────────────┘  └──────────────────────┘        │
│                    │                  │                      │
│                    ▼                  ▼                      │
│  ┌──────────────────────────────────────────────────────┐  │
│  │                    采集服务集群                        │  │
│  │  ┌────────────┐  ┌────────────┐  ┌────────────┐     │  │
│  │  │采集进程 #1  │  │采集进程 #2  │  │采集进程 #N  │     │  │
│  │  │(Modbus)   │  │(IEC104)    │  │(IEC61850) │     │  │
│  │  └────────────┘  └────────────┘  └────────────┘     │  │
│  └──────────────────────────────────────────────────────┘  │
│                              │                              │
│                              ▼                              │
│  ┌──────────────────────────────────────────────────────┐  │
│  │                    策略服务集群                        │  │
│  │  ┌────────────┐  ┌────────────┐  ┌────────────┐     │  │
│  │  │策略进程 #1  │  │策略进程 #2  │  │策略进程 #N  │     │  │
│  │  └────────────┘  └────────────┘  └────────────┘     │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

## 8. 安全性设计

### 8.1 认证授权

- **用户认证**：支持用户名密码、Token、证书等多种认证方式
- **权限控制**：基于RBAC模型的细粒度权限控制
- **操作审计**：记录所有操作日志，支持追溯

### 8.2 通信安全

- **传输加密**：支持TLS/SSL加密通信
- **协议安全**：Modbus TCP签名、IEC61850安全通信
- **访问控制**：IP白名单、网络分段

### 8.3 数据安全

- **数据加密**：敏感数据加密存储
- **备份恢复**：定期备份，支持快速恢复
- **数据脱敏**：支持脱敏展示

## 9. 监控与运维

### 9.1 系统监控

- **进程监控**：监控各进程运行状态、心跳检测
- **性能监控**：CPU、内存、网络、磁盘监控
- **业务监控**：采集成功率、数据延迟、告警及时性

### 9.2 日志管理

```
┌──────────────────────────────────────────────────────┐
│                  日志架构                              │
├──────────────────────────────────────────────────────┤
│                                                      │
│  各进程 ──▶ 本地日志文件                               │
│     │         /var/log/ems/{process}/                │
│     │                                               │
│     ▼                                               │
│  日志收集器 ──▶ ELK/ Loki                            │
│     │                                               │
│     ▼                                               │
│  日志存储 ──▶ Elasticsearch / Loki                  │
│     │                                               │
│     ▼                                               │
│  可视化 ──▶ Grafana                                  │
│                                                      │
└──────────────────────────────────────────────────────┘
```

### 9.3 告警机制

- **系统告警**：进程异常、资源不足、连接断开
- **业务告警**：数据异常、策略触发、告警升级
- **通知方式**：邮件、短信、WebHook

## 10. 扩展性设计

### 10.1 协议扩展

新增通讯协议只需实现对应的适配器接口：

```python
class ProtocolAdapter(ABC):
    @abstractmethod
    def connect(self) -> bool:
        pass
    
    @abstractmethod
    def disconnect(self):
        pass
    
    @abstractmethod
    def read_data(self, point_config: dict) -> Optional[PointData]:
        pass
    
    @abstractmethod
    def write_data(self, point_config: dict, value: Any) -> bool:
        pass
    
    @abstractmethod
    def heartbeat(self) -> bool:
        pass
```

### 10.2 策略扩展

支持插件化的策略扩展：

```python
class StrategyPlugin(ABC):
    @abstractmethod
    def evaluate(self, context: StrategyContext) -> List[Action]:
        pass
    
    @abstractmethod
    def execute(self, action: Action) -> ActionResult:
        pass
```

### 10.3 存储扩展

支持多种存储后端：

- **关系型数据库**：MySQL/PostgreSQL（配置、用户数据）
- **时序数据库**：TDengine/InfluxDB（历史数据）
- **缓存数据库**：Redis/Memcached（实时数据）
- **消息队列**：Kafka/RabbitMQ（大规模数据流）

## 11. 技术选型

### 11.1 核心组件

| 组件 | 技术选型 | 版本要求 | 说明 |
|------|---------|---------|------|
| 编程语言 | Python | 3.10+ | 核心开发语言 |
| Web框架 | FastAPI | 0.100+ | 高性能API服务 |
| 前端框架 | Vue3 | 3.3+ | 现代Web界面 |
| 实时数据库 | Redis | 7.0+ | 实时数据缓存 |
| 时序数据库 | TDengine | 3.0+ | 时序数据存储 |
| 消息队列 | Redis Stream | 7.0+ | 进程间通信 |
| 协议库 | pymodbus | 2.5+ | Modbus协议 |
| 协议库 | lib60870.NET | 2.0+ | IEC104协议 |

### 11.2 基础设施

| 组件 | 技术选型 | 说明 |
|------|---------|------|
| 容器化 | Docker | 应用容器化 |
| 编排 | Docker Compose / K8S | 服务编排 |
| 反向代理 | Nginx | 负载均衡 |
| 日志收集 | ELK / Loki | 日志管理 |
| 监控 | Prometheus + Grafana | 系统监控 |

## 12. 附录

### 12.1 术语表

| 术语 | 英文 | 说明 |
|------|------|------|
| EMS | Energy Management System | 能源管理系统 |
| RTDB | Real-Time Database | 实时数据库 |
| SCADA | Supervisory Control And Data Acquisition | 数据采集与监视控制系统 |
| IEC | International Electrotechnical Commission | 国际电工委员会 |
| DLT | Electric Power Metering | 中国电力行业标准 |

### 12.2 参考标准

- IEC 60870-5-104: 运动设备与系统 - 第5-104部分
- IEC 61850: 变电站通信网络和系统
- DLT 645-2007: 多功能电能表通信协议
- Modbus IDA: Modbus协议规范

### 12.3 文档修订记录

| 版本 | 日期 | 作者 | 说明 |
|------|------|------|------|
| 1.0 | 2026-04-01 | System | 初始版本 |
