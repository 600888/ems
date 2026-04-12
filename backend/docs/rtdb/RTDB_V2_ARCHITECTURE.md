# EMS 实时数据库架构 V2 - 多进程共享方案

## 📋 概述

本文档描述EMS系统中实时数据库（RTDB）的V2架构，采用**方案2**实现多进程间的测点数据共享。

## 🏗️ 整体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                     Process 1: 数据采集                          │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐    │
│  │ 测点配置缓存 │     │ RTDB Manager │     │  Redis Client│    │
│  │ (本地副本)   │ ←→  │              │ ←→  │              │    │
│  └──────────────┘     └──────────────┘     └──────┬───────┘    │
│                                                   │              │
├──────────────────────────────────────────────────┼──────────────┤
│                                                   ↓              │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                    Redis Server                              ││
│  │                                                              ││
│  │  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐            ││
│  │  │ rtdb:yc:*   │ │ rtdb:yx:*   │ │ rtdb:yk:*   │            ││
│  │  │ (遥测数据)  │ │ (遥信数据)  │ │ (遥控数据)  │            ││
│  │  └─────────────┘ └─────────────┘ └─────────────┘            ││
│  │                                                              ││
│  │  ┌─────────────┐                                            ││
│  │  │ rtdb:yt:*   │ (遥调数据)                                   ││
│  │  └─────────────┘                                            ││
│  └─────────────────────────────────────────────────────────────┘│
│                                                   ↑              │
│                                                   │              │
├──────────────────────────────────────────────────┼──────────────┤
│                     Process 2: API服务                          │
│  ┌──────────────┐     ┌──────────────┐     ┌──────┴───────┐    │
│  │ 测点配置缓存 │     │ RTDB Manager │     │  Redis Client│    │
│  │ (本地副本)   │ ←→  │              │ ←→  │              │    │
│  └──────────────┘     └──────────────┘     └──────────────┘    │
│                                                                  │
├──────────────────────────────────────────────────────────────────┤
│                     Process 3: 告警服务                          │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐    │
│  │ 测点配置缓存 │     │ RTDB Manager │     │  Redis Client│    │
│  │ (本地副本)   │ ←→  │              │ ←→  │              │    │
│  └──────────────┘     └──────────────┘     └──────────────┘    │
└──────────────────────────────────────────────────────────────────┘
```

## 📦 核心组件

### 1. RTDB（Redis实时数据）

**职责**: 存储和管理实时数据

**文件**: 
- [rtdb.h](file:///home/cdy/ems/backend/src/rtdb/rtdb.h) - RTDB接口
- [rtdb.cpp](file:///home/cdy/ems/backend/src/rtdb/rtdb.cpp) - RTDB实现
- [rtdb_point.h](file:///home/cdy/ems/backend/inc/rtdb/rtdb_point.h) - 精简数据结构

**数据结构**:

```cpp
struct YCPoint {
    double value{0.0};      // 工程值
    uint64_t timestamp{0};  // 时间戳
    int quality{0};          // 质量
    int status{0};          // 状态
};

struct YXPoint {
    int value{0};           // 0或1
    uint64_t timestamp{0};
    int quality{0};
    int status{0};
};

struct YKPoint {
    int value{0};
    int command_type{0};
    uint64_t timestamp{0};
    YKStatus status{YKStatus::Idle};
    int result{0};
};

struct YTPoint {
    double value{0.0};
    uint64_t timestamp{0};
    int quality{0};
    int status{0};
};
```

**Redis Key设计**:

```
ems:rtdb:yc:{point_code}  → 遥测数据
ems:rtdb:yx:{point_code}  → 遥信数据
ems:rtdb:yk:{point_code}  → 遥控数据
ems:rtdb:yt:{point_code}  → 遥调数据
```

### 2. RTDBConfigCache（配置缓存）

**职责**: 本地缓存测点配置

**文件**: 
- [rtdb_cache.h](file:///home/cdy/ems/backend/inc/rtdb/rtdb_cache.h) - 配置缓存接口
- [rtdb_cache.cpp](file:///home/cdy/ems/backend/src/rtdb/rtdb_cache.cpp) - 配置缓存实现

**特点**:
- 线程安全的读写锁（shared_mutex）
- 各进程独立副本
- 支持配置变更通知

**数据结构**:

```cpp
struct RTDBConfigCache::YCConfig {
    std::string code;
    std::string name;
    int64_t channel_id{0};
    double mul_coe{1.0};
    double add_coe{0.0};
    double max_limit{9999999.0};
    double min_limit{-9999999.0};
    std::string unit;
    int status{0};
};
```

### 3. RTDBPointManager（测点管理器）

**职责**: 组合配置和数据

**文件**: 
- [rtdb_cache.h](file:///home/cdy/ems/backend/inc/rtdb/rtdb_cache.h) - 包含在RTDBConfigCache中

**功能**:
- 组合配置和实时数据
- 数据转换（系数处理）
- 数据验证（限值检查）

```cpp
struct YCFullPoint {
    RTDBConfigCache::YCConfig config;
    YCPoint data;
};
```

### 4. RTDBManager（统一管理器）

**职责**: 统一管理配置、数据和事件

**文件**: 
- [rtdb_manager.h](file:///home/cdy/ems/backend/inc/rtdb/rtdb_manager.h) - RTDBManager接口
- [rtdb_manager.cpp](file:///home/cdy/ems/backend/src/rtdb/rtdb_manager.cpp) - RTDBManager实现

**功能**:
- 初始化和生命周期管理
- 配置加载
- 单个和批量数据更新
- 数据变更通知

## 🔧 使用示例

### 示例1: 基础使用

```cpp
#include "rtdb/rtdb_manager.h"

void basic_usage() {
    RTDBConfig config;
    config.host = "localhost";
    config.port = 6379;
    config.password = "888888";
    config.pool_size = 10;
    
    auto manager = createRTDBManager(config);
    if (!manager || !manager->isConnected()) {
        std::cerr << "连接失败: " << manager->getLastError() << std::endl;
        return;
    }
    
    std::cout << "✓ Redis连接成功" << std::endl;
}
```

### 示例2: 配置加载

```cpp
void load_configs() {
    auto manager = createRTDBManager(config);
    auto config_cache = manager->getConfigCache();
    
    std::vector<RTDBConfigCache::YCConfig> yc_configs = {
        {"voltage_a", "A相电压", 1, 0.1, 0.0, 250.0, 0.0, "V"},
        {"voltage_b", "B相电压", 1, 0.1, 0.0, 250.0, 0.0, "V"},
        {"voltage_c", "C相电压", 1, 0.1, 0.0, 250.0, 0.0, "V"},
    };
    config_cache->loadYCConfigs(yc_configs);
    
    std::cout << "加载了 " << config_cache->getYCConfigCount() << " 个遥测配置" << std::endl;
}
```

### 示例3: 数据更新

```cpp
void update_data() {
    auto manager = createRTDBManager(config);
    
    YCPoint yc;
    yc.value = 220.5;
    yc.timestamp = std::time(nullptr);
    yc.quality = 0;
    yc.status = 0;
    
    if (manager->updateYC("voltage_a", yc)) {
        std::cout << "✓ 数据更新成功: " << yc.value << "V" << std::endl;
    }
    
    YCPoint read_yc;
    if (manager->getYC("voltage_a", read_yc)) {
        std::cout << "✓ 数据读取成功: " << read_yc.value << "V" << std::endl;
    }
}
```

### 示例4: 完整测点查询（配置+数据）

```cpp
void query_full_point() {
    auto manager = createRTDBManager(config);
    auto config_cache = manager->getConfigCache();
    
    config_cache->loadYCConfigs({
        {"voltage_a", "A相电压", 1, 0.1, 0.0, 250.0, 0.0, "V"}
    });
    
    YCPoint yc_data = {220.5, std::time(nullptr), 0, 0};
    manager->updateYC("voltage_a", yc_data);
    
    RTDBPointManager::YCFullPoint full_point;
    if (manager->getYCFull("voltage_a", full_point)) {
        std::cout << "测点: " << full_point.config.name << std::endl;
        std::cout << "值: " << full_point.data.value << full_point.config.unit << std::endl;
        std::cout << "限值: " << full_point.config.min_limit 
                  << " ~ " << full_point.config.max_limit << std::endl;
    }
}
```

### 示例5: 批量操作

```cpp
void batch_update() {
    auto manager = createRTDBManager(config);
    
    std::map<std::string, YCPoint> yc_batch;
    for (int i = 0; i < 10; ++i) {
        YCPoint yc = {200.0 + i * 10.0, std::time(nullptr), 0, 0};
        yc_batch["voltage_" + std::to_string(i)] = yc;
    }
    
    if (manager->batchUpdateYC(yc_batch)) {
        std::cout << "✓ 批量更新 " << yc_batch.size() << " 个测点成功" << std::endl;
    }
}
```

### 示例6: 数据变更通知

```cpp
void data_change_notification() {
    auto manager = createRTDBManager(config);
    
    manager->setDataChangeCallback([](const std::string& point_type,
                                     const std::string& point_code,
                                     const void* data) {
        std::cout << "数据变更: " << point_type << ":" << point_code << std::endl;
    });
    
    YCPoint yc = {220.0, std::time(nullptr), 0, 0};
    manager->updateYC("voltage_a", yc);
}
```

### 示例7: 告警检测

```cpp
void alarm_detection() {
    auto manager = createRTDBManager(config);
    auto point_manager = manager->getPointManager();
    auto config_cache = manager->getConfigCache();
    
    config_cache->loadYCConfigs({
        {"voltage_a", "A相电压", 1, 1.0, 0.0, 240.0, 180.0, "V"}
    });
    
    YCPoint yc = {260.0, std::time(nullptr), 0, 0};
    manager->updateYC("voltage_a", yc);
    
    RTDBPointManager::YCFullPoint full_point;
    if (manager->getYCFull("voltage_a", full_point)) {
        bool is_valid = point_manager->validateYCValue(
            full_point.data.value, 
            full_point.config
        );
        
        if (!is_valid) {
            std::cout << "⚠️ 告警: 电压超出范围!" << std::endl;
        }
    }
}
```

## 📊 性能对比

| 指标 | 共享内存方案 | Redis方案（V2） |
|------|------------|----------------|
| **内存占用** | ~200MB/进程 | ~30MB + 20MB×N |
| **实现复杂度** | 高 | 低 |
| **一致性** | 需手动管理 | Redis保证 |
| **分布式支持** | 不支持 | 支持 |
| **访问延迟** | ~0.01ms | ~0.5-1ms |
| **并发控制** | 复杂 | Redis自动 |

## 🎯 适用场景

### 场景1: 多进程数据采集

```cpp
void collector_process() {
    auto manager = createRTDBManager(config);
    auto config_cache = manager->getConfigCache();
    
    load_configs_from_db(config_cache);
    
    while (running) {
        for (const auto& code : config_cache->getYCConfigCodes()) {
            auto cfg = get_config(code);
            auto raw_value = read_from_device(cfg);
            double value = raw_value * cfg.mul_coe + cfg.add_coe;
            
            YCPoint yc;
            yc.value = value;
            yc.timestamp = std::time(nullptr);
            yc.quality = check_quality(value, cfg);
            manager->updateYC(code, yc);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
```

### 场景2: API服务

```cpp
void api_process() {
    auto manager = createRTDBManager(config);
    auto config_cache = manager->getConfigCache();
    
    load_configs_from_db(config_cache);
    
    register_api("/api/yc/:code", [](const std::string& code) {
        RTDBPointManager::YCFullPoint full_point;
        if (manager->getYCFull(code, full_point)) {
            return json{
                {"code", full_point.config.code},
                {"name", full_point.config.name},
                {"value", full_point.data.value},
                {"unit", full_point.config.unit},
                {"timestamp", full_point.data.timestamp},
                {"quality", full_point.data.quality}
            };
        }
        return json{{"error", "Point not found"}};
    });
}
```

### 场景3: 告警检测

```cpp
void alarm_process() {
    auto manager = createRTDBManager(config);
    auto point_manager = manager->getPointManager();
    auto config_cache = manager->getConfigCache();
    
    load_configs_from_db(config_cache);
    
    while (running) {
        for (const auto& code : config_cache->getYCConfigCodes()) {
            RTDBPointManager::YCFullPoint full_point;
            if (manager->getYCFull(code, full_point)) {
                bool is_valid = point_manager->validateYCValue(
                    full_point.data.value,
                    full_point.config
                );
                
                if (!is_valid) {
                    trigger_alarm(full_point);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
```

## 🔍 Redis管理命令

```bash
redis-cli

# 查看所有测点
KEYS ems:rtdb:*

# 查看遥测数据
HGETALL ems:rtdb:yc:voltage_a

# 统计测点数量
DBSIZE
KEYS ems:rtdb:yc:* | wc -l

# 清理所有数据
DEL $(KEYS "ems:rtdb:*")

# 监控实时操作
MONITOR
```

## ⚙️ 配置选项

```cpp
RTDBConfig config;
config.host = "localhost";           // Redis主机
config.port = 6379;                 // Redis端口
config.password = "888888";         // Redis密码
config.db = 0;                      // 数据库编号
config.pool_size = 10;               // 连接池大小
config.timeout_ms = 5000;           // 超时时间(ms)
config.prefix = "";                  // Key前缀
```

## 📈 内存估算

假设系统有100,000个测点：

| 组件 | 单测点大小 | 总大小 | 说明 |
|------|----------|--------|------|
| **测点配置副本** | ~200B | 20MB | 各进程独立，可接受 |
| **Redis实时数据** | ~100B | 10MB | 所有进程共享 |
| **总内存** | - | 30MB + 20MB×N | N=进程数 |

## 🔄 更新日志

### V2.0 (2026-04-02)
- 实现分层架构：配置缓存 + Redis实时数据
- 添加RTDBConfigCache配置缓存
- 添加RTDBPointManager测点管理
- 添加RTDBManager统一管理器
- 支持数据变更通知
- 支持批量操作

## 💡 最佳实践

1. **配置加载时机**: 进程启动时一次性加载，避免运行时频繁查询
2. **批量操作**: 优先使用批量更新接口，减少网络往返
3. **数据验证**: 使用PointManager进行数据验证和转换
4. **错误处理**: 始终检查返回值和错误信息
5. **监控**: 使用Redis MONITOR监控数据变化

## 🐛 故障排除

### 连接失败

```cpp
if (!manager || !manager->isConnected()) {
    std::cerr << "Redis连接失败: " << manager->getLastError() << std::endl;
    // 检查Redis服务: systemctl status redis
    // 测试连接: redis-cli ping
}
```

### 数据不一致

```cpp
// 定期检查配置和数据的一致性
void check_consistency() {
    auto config_cache = manager->getConfigCache();
    auto rtdb = manager->getRTDB();
    
    auto config_codes = config_cache->getYCConfigCodes();
    auto data_codes = rtdb->getYCCodes();
    
    std::vector<std::string> diff;
    std::set_difference(config_codes.begin(), config_codes.end(),
                       data_codes.begin(), data_codes.end(),
                       std::back_inserter(diff));
    
    if (!diff.empty()) {
        std::cerr << "警告: " << diff.size() << " 个配置缺少数据" << std::endl;
    }
}
```

## 🎓 总结

V2架构通过分离测点配置和实时数据，实现了：

✅ **内存优化**: 测点配置仅存本地副本，Redis仅存精简实时数据

✅ **多进程共享**: 通过Redis实现进程间数据共享

✅ **性能提升**: 配置本地访问，实时数据Redis共享

✅ **易于维护**: 分层设计，职责清晰

✅ **可扩展性**: 支持分布式部署和水平扩展
