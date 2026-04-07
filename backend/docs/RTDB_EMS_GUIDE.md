# EMS工商储实时数据库使用指南

## 📋 概述

基于EMS工商储场景设计的实时数据库，采用Redis作为核心存储，实现了针对储能系统的数据实时采集、控制指令下发、设备互斥锁等核心功能。

## 🏗️ 架构设计

```
┌─────────────────────────────────────────────────────────────┐
│                      设备感知与接入层                         │
│  PCS (功率转换系统) │ BMS (电池管理) │ 电表 │ 传感器         │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│                      Redis 核心存储层                         │
│                                                              │
│  ┌─────────────────┐  ┌─────────────────┐                   │
│  │ 设备实时快照    │  │ 时序数据缓冲    │                   │
│  │ Hash            │  │ Hash            │                   │
│  │ ems:device:*    │  │ ems:device:*   │                   │
│  └─────────────────┘  └─────────────────┘                   │
│                                                              │
│  ┌─────────────────┐  ┌─────────────────┐                   │
│  │ 控制指令队列    │  │ 设备控制互斥锁  │                   │
│  │ List            │  │ String + TTL    │                   │
│  │ ems:cmd:*       │  │ ems:lock:ctrl:*│                   │
│  └─────────────────┘  └─────────────────┘                   │
│                                                              │
│  ┌─────────────────┐                                        │
│  │ 系统拓扑缓存    │                                        │
│  │ Hash            │                                        │
│  │ ems:config:*   │                                        │
│  └─────────────────┘                                        │
└─────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────┐
│                      业务应用层                               │
│  EMS策略引擎 │ 监控大屏 │ 告警服务 │ 数据同步服务              │
└─────────────────────────────────────────────────────────────┘
```

## 🔧 快速开始

### 1. 初始化连接

```cpp
#include "rtdb/rtdb_ems.h"

RTDBConfig config;
config.host = "localhost";
config.port = 6379;
config.password = "888888";
config.pool_size = 10;
config.prefix = "ems:";

auto rtdb = createRTDBEMS(config);
if (!rtdb || !rtdb->isConnected()) {
    std::cerr << "连接失败: " << rtdb->getLastError() << std::endl;
    return;
}

std::cout << "✓ Redis连接成功" << std::endl;
```

### 2. 设备实时快照

设备实时快照用于存储单个设备的所有测点数据（电压、电流、SOC、功率等），适合监控展示场景。

#### 保存设备快照

```cpp
DeviceSnapshot pcs_snapshot;
pcs_snapshot.device_id = "pcs_001";
pcs_snapshot.device_type = "PCS";
pcs_snapshot.timestamp = std::time(nullptr);
pcs_snapshot.quality = 0;

// 遥测数据 (YC)
pcs_snapshot.yc_values["voltage_a"] = 220.5;
pcs_snapshot.yc_values["voltage_b"] = 221.0;
pcs_snapshot.yc_values["voltage_c"] = 219.8;
pcs_snapshot.yc_values["current_a"] = 45.2;
pcs_snapshot.yc_values["power"] = 29.5;
pcs_snapshot.yc_values["soc"] = 75.5;

// 遥信数据 (YX)
pcs_snapshot.yx_values["running"] = 1;
pcs_snapshot.yx_values["charging"] = 0;
pcs_snapshot.yx_values["discharging"] = 1;
pcs_snapshot.yx_values["fault"] = 0;

// 元数据
pcs_snapshot.metadata["manufacturer"] = "华为";
pcs_snapshot.metadata["model"] = "SUN2000-100KTL";

if (rtdb->setDeviceSnapshot("pcs_001", pcs_snapshot)) {
    std::cout << "✓ 设备快照保存成功" << std::endl;
}
```

#### 读取设备快照

```cpp
DeviceSnapshot read_snapshot;
if (rtdb->getDeviceSnapshot("pcs_001", read_snapshot)) {
    std::cout << "SOC: " << read_snapshot.yc_values["soc"] << "%" << std::endl;
    std::cout << "功率: " << read_snapshot.yc_values["power"] << " kW" << std::endl;
    std::cout << "运行状态: " << (read_snapshot.yx_values["running"] ? "运行" : "停止") << std::endl;
}
```

#### 更新单个测点

```cpp
// 更新遥测点
rtdb->updateDevicePoint("pcs_001", "power", 30.5);

// 更新遥信点
rtdb->updateDevicePoint("pcs_001", "running", 1);

// 读取单个测点
double power;
if (rtdb->getDevicePoint("pcs_001", "power", power)) {
    std::cout << "当前功率: " << power << " kW" << std::endl;
}
```

## ⏱️ 时序数据缓冲

对于高频采集场景（如波形数据），可以使用时序数据缓冲功能暂存数据，后续由同步服务批量写入时序数据库。

#### 写入时序数据

```cpp
std::string device_id = "pcs_001";
std::string point_code = "power";

for (int i = 0; i < 10; ++i) {
    double power = 25.0 + (i % 5) * 2.0;
    uint64_t timestamp = std::time(nullptr) - (10 - i) * 360;
    rtdb->appendTimeSeriesData(device_id, point_code, power, timestamp);
}

std::cout << "✓ 时序数据写入成功" << std::endl;
```

#### 读取时序数据

```cpp
auto history = rtdb->getTimeSeriesData(
    device_id,
    point_code,
    0,              // 起始时间（0表示不限）
    0,              // 结束时间（0表示不限）
    5               // 限制返回数量
);

for (const auto& [timestamp, value] : history) {
    std::cout << timestamp << ": " << value << " kW" << std::endl;
}
```

## 📤 控制指令队列

EMS核心策略通过指令队列下发控制指令到驱动层执行，支持ACK确认机制。

#### 下发控制指令

```cpp
DeviceCommand cmd;
cmd.command_id = "cmd_001";
cmd.device_id = "pcs_001";
cmd.command_type = "set_power";
cmd.point_code = "power";
cmd.value = 25.0;                    // 目标功率 25kW
cmd.priority = 1;
cmd.timestamp = std::time(nullptr);
cmd.source = "strategy_peak_shaving";  // 来源：削峰策略

if (rtdb->pushCommand(cmd)) {
    std::cout << "✓ 控制指令已下发" << std::endl;
}
```

#### 驱动层消费指令

```cpp
DeviceCommand cmd;
if (rtdb->popCommand("pcs_001", cmd, 0)) {
    std::cout << "收到指令: " << cmd.command_type 
              << ", 值: " << cmd.value << std::endl;
    
    // 执行控制逻辑...
    
    // 确认执行结果
    rtdb->ackCommand(cmd.command_id, true, "执行成功");
}
```

#### 查询指令执行结果

```cpp
CommandResult result;
if (rtdb->getCommandResult("cmd_001", result)) {
    std::cout << "执行结果: " << (result.success ? "成功" : "失败")
              << ", 消息: " << result.message << std::endl;
}
```

## 🔒 设备控制互斥锁

防止多个策略线程同时控制同一台设备，确保控制安全性。

#### 获取设备控制权

```cpp
std::string device_id = "pcs_001";
std::string owner_id = "strategy_peak_shaving";

if (rtdb->acquireControlLock(device_id, owner_id, 10)) {  // 10秒超时
    std::cout << "✓ 获取设备控制权成功" << std::endl;
    
    // 执行控制操作...
    
    rtdb->releaseControlLock(device_id, owner_id);
} else {
    std::cout << "✗ 获取设备控制权失败（设备正被其他策略控制）" << std::endl;
}
```

#### 检查设备锁定状态

```cpp
if (rtdb->isLocked("pcs_001")) {
    std::string owner = rtdb->getLockOwner("pcs_001");
    std::cout << "设备已被锁定，当前控制者: " << owner << std::endl;
}
```

#### 原子性执行控制操作

```cpp
rtdb->executeWithLock(
    device_id,
    owner_id,
    []() {
        // 需要原子性执行的控制逻辑
        std::cout << "执行削峰策略: 降低功率到20kW" << std::endl;
        return true;
    },
    5000  // 5秒超时
);
```

#### 典型应用场景

**场景1：削峰填谷策略**

```cpp
bool executePeakValleyStrategy(const std::string& strategy_id) {
    return rtdb->executeWithLock(
        "pcs_001",
        strategy_id,
        []() {
            // 读取当前SOC
            double soc;
            if (!rtdb->getDevicePoint("pcs_001", "soc", soc)) {
                return false;
            }
            
            // 根据SOC判断充放电
            if (soc > 80) {
                // SOC高，放电
                DeviceCommand cmd;
                cmd.device_id = "pcs_001";
                cmd.command_type = "set_power";
                cmd.value = -30.0;  // 放电30kW
                rtdb->pushCommand(cmd);
            } else if (soc < 20) {
                // SOC低，充电
                DeviceCommand cmd;
                cmd.device_id = "pcs_001";
                cmd.command_type = "set_power";
                cmd.value = 30.0;   // 充电30kW
                rtdb->pushCommand(cmd);
            }
            
            return true;
        },
        5000
    );
}
```

**场景2：防止并发控制**

```cpp
void multipleStrategyExample() {
    std::string owner_1 = "strategy_peak_shaving";
    std::string owner_2 = "strategy_valley";
    
    // 策略1获取控制权
    if (rtdb->acquireControlLock("pcs_001", owner_1, 10)) {
        std::cout << "策略1执行削峰操作" << std::endl;
        rtdb->releaseControlLock("pcs_001", owner_1);
    }
    
    // 策略2尝试获取控制权（会失败）
    if (!rtdb->acquireControlLock("pcs_001", owner_2, 1)) {
        std::cout << "策略2等待，设备正被策略1控制" << std::endl;
    }
}
```

## 🗺️ 系统拓扑缓存

缓存系统拓扑结构和静态配置，减少MySQL查询压力。

#### 保存系统拓扑

```cpp
SystemTopology topology;
topology.topology_id = "topo_001";
topology.name = "工业园区储能系统";

topology.device_ids = {"pcs_001", "bms_001", "meter_001"};

topology.relationships["pcs_001"] = "bms_001";
topology.relationships["pcs_001"] = "meter_001";

topology.metadata["site_name"] = "工业园区A";
topology.metadata["capacity"] = "500kWh";
topology.metadata["rated_power"] = "100kW";

if (rtdb->setTopology(topology)) {
    std::cout << "✓ 系统拓扑保存成功" << std::endl;
}
```

#### 读取系统拓扑

```cpp
SystemTopology read_topo;
if (rtdb->getTopology("topo_001", read_topo)) {
    std::cout << "拓扑名称: " << read_topo.name << std::endl;
    std::cout << "设备列表:" << std::endl;
    for (const auto& device_id : read_topo.device_ids) {
        std::cout << "  - " << device_id << std::endl;
    }
    std::cout << "装机容量: " << read_topo.metadata["capacity"] << std::endl;
}
```

#### 获取所有拓扑

```cpp
std::vector<SystemTopology> all_topologies;
if (rtdb->getAllTopologies(all_topologies)) {
    for (const auto& topo : all_topologies) {
        std::cout << topo.name << std::endl;
    }
}
```

## 📊 Redis Key 设计

### Key 命名规范

| 业务场景 | Redis 结构 | Key 命名示例 | 说明 |
|---------|-----------|-------------|------|
| 设备实时快照 | Hash | `ems:device:{deviceId}:rtdata` | 存储单台设备的所有测点 |
| 时序数据缓冲 | Hash | `ems:device:{deviceId}:history:{pointCode}` | 暂存近期波形数据 |
| 控制指令队列 | List | `ems:cmd:{deviceId}:queue` | 下发控制指令队列 |
| 指令执行结果 | Hash | `ems:cmd:result:{commandId}` | 存储指令执行结果 |
| 设备控制互斥锁 | String | `ems:lock:ctrl:{deviceId}` | 防止并发控制 |
| 系统拓扑 | Hash | `ems:config:topology:{topologyId}` | 缓存系统拓扑结构 |
| 拓扑索引 | Hash | `ems:config:topology:index` | 拓扑ID索引 |

### Hash 字段设计

#### 设备实时快照

```
Key: ems:device:pcs_001:rtdata
Hash: {
  "device_id": "pcs_001",
  "device_type": "PCS",
  "timestamp": "1712001234",
  "quality": "0",
  "yc:voltage_a": "220.5",
  "yc:voltage_b": "221.0",
  "yc:current_a": "45.2",
  "yc:soc": "75.5",
  "yx:running": "1",
  "yx:charging": "0",
  "yx:discharging": "1",
  "meta:manufacturer": "华为",
  "meta:model": "SUN2000-100KTL"
}

TTL: 30秒（数据过期机制）
```

## ⚙️ 配置说明

### RTDBConfig 参数

```cpp
struct RTDBConfig {
    std::string host;      // Redis服务器地址（默认：localhost）
    int port;              // Redis端口（默认：6379）
    std::string password;  // Redis密码（默认：888888）
    int db;                // 数据库编号（默认：0）
    int pool_size;         // 连接池大小（默认：10）
    int timeout_ms;        // 超时时间（默认：5000ms）
    std::string prefix;    // Key前缀（默认：ems:）
};
```

## 🔍 最佳实践

### 1. 数据过期策略

设备实时快照建议设置TTL（如30秒），如果驱动服务挂掉，前端能感知到数据过期，而不是显示滞后的错误数据。

```cpp
// 在保存快照后自动设置过期时间
rtdb_->expire(buildDeviceSnapshotKey(device_id), 30);
```

### 2. 读写分离

- **驱动服务写Master**：保证数据写入可靠性
- **EMS策略和大屏读Slave**：减少Master压力，提高读取性能

### 3. 控制安全

- **使用互斥锁**：确保同一时间只有一个策略能控制设备
- **SOC检查**：放电前检查SOC是否满足条件
- **ACK确认**：驱动层执行完指令后确认执行结果

### 4. 批量操作

```cpp
// 批量更新设备测点
DeviceSnapshot snapshot;
// ... 设置snapshot数据
rtdb->setDeviceSnapshot(device_id, snapshot);

// 比逐个调用updateDevicePoint更高效
```

### 5. 异常处理

```cpp
if (!rtdb || !rtdb->isConnected()) {
    std::cerr << "Redis连接失败: " << rtdb->getLastError() << std::endl;
    // 降级处理或重连逻辑
    return;
}
```

## 📝 完整示例

查看完整使用示例：[rtdb_ems_usage.cpp](file:///home/cdy/ems/backend/examples/rtdb_ems_usage.cpp)

示例包含：
- 设备实时快照的保存和读取
- 控制指令的下发和ACK确认
- 设备控制互斥锁的使用
- 时序数据的写入和查询
- 系统拓扑的缓存和管理

## 🔗 相关文档

- [RTDB架构设计](file:///home/cdy/ems/backend/docs/RTDB_ARCHITECTURE.md)
- [RTDB V2架构文档](file:///home/cdy/ems/backend/docs/RTDB_V2_ARCHITECTURE.md)
