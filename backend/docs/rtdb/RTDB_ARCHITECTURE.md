# EMS 实时数据库架构设计

## 概述

本文档描述EMS系统中实时数据库（RTDB）的架构设计，采用分层存储策略，将测点配置数据与实时数据分离，以优化性能和资源利用。

## 分层架构

```
┌─────────────────────────────────────────────────────┐
│  Layer 1: 测点配置数据 (Point Models)                │
│  - 存储位置: MySQL/PostgreSQL                       │
│  - 包含: 完整配置信息                               │
│  - 特点: 静态数据，很少变化                         │
│  - 文件: backend/inc/models/point_*.h              │
└─────────────────────────────────────────────────────┘
                      ↓ 映射(point_code)
┌─────────────────────────────────────────────────────┐
│  Layer 2: 实时数据 (RTDB - Redis)                  │
│  - 存储位置: Redis Hash                            │
│  - 包含: 实时值、时间戳、质量、状态                 │
│  - 特点: 高频更新，仅存储必需字段                   │
│  - 文件: backend/inc/rtdb/rtdb.h                  │
└─────────────────────────────────────────────────────┘
                      ↓ 可选
┌─────────────────────────────────────────────────────┐
│  Layer 3: 共享内存 (可选，高性能场景)               │
│  - 存储位置: 共享内存                              │
│  - 包含: 实时值快照                                 │
│  - 特点: 超低延迟，需要额外同步机制                 │
└─────────────────────────────────────────────────────┘
```

## 数据分离原则

### 静态配置数据（测点模型）

**存储位置**: 数据库（MySQL/PostgreSQL）

**包含字段**:
- 测点标识: code, name, id
- 通信配置: rtu_addr, reg_addr, func_code, decode_code
- 数据处理: mul_coe, add_coe, max_limit, min_limit
- 协议配置: iec_common_address, iec_cot, iec_quality
- 关联关系: channel_id, related_yc_id, related_yx_id
- 其他: unit, description, bit, reverse, command_type

**特点**:
- 数据量大（每个测点约2KB）
- 变化频率低（仅配置时修改）
- 需要持久化存储
- 支持复杂查询和关系映射

### 动态实时数据（RTDB）

**存储位置**: Redis

**包含字段**:
- 值: value
- 时间戳: timestamp
- 质量: quality
- 状态: status
- 控制类型（仅YK）: command_type
- 控制结果（仅YK）: result

**特点**:
- 数据量小（每个测点约50-100字节）
- 变化频率高（毫秒级更新）
- 内存存储，性能优先
- 支持高频读写

## Redis数据结构

### Key命名规范

```
ems:rtdb:{type}:{point_code}
```

**示例**:
- `ems:rtdb:yc:voltage_a` - 遥测点
- `ems:rtdb:yx:breaker_001` - 遥信点
- `ems:rtdb:yk:yk_breaker_001` - 遥控点
- `ems:rtdb:yt:setpoint_001` - 遥调点

### Hash字段设计

#### 遥测YC（4个字段）

```
Key: ems:rtdb:yc:{point_code}
Hash: {
  "v": "220.5",        // value: 工程值 (double)
  "t": "1712001234",   // timestamp: Unix时间戳 (uint64)
  "q": "0",            // quality: 0=好, 1=无效, 2=保留, 3=错误
  "s": "0"             // status: 状态标志
}
```

**内存占用**: ~50字节/测点

#### 遥信YX（4个字段）

```
Key: ems:rtdb:yx:{point_code}
Hash: {
  "v": "1",            // value: 0或1 (int)
  "t": "1712001234",
  "q": "0",
  "s": "0"
}
```

**内存占用**: ~50字节/测点

#### 遥控YK（5个字段）

```
Key: ems:rtdb:yk:{point_code}
Hash: {
  "v": "1",            // value: 控制值
  "t": "1712001234",
  "c": "0",            // command_type: 0=pulse, 1=latch, 2=step
  "s": "0",            // status: 0=空闲, 1=待执行, 2=执行中, 3=成功, 4=失败
  "r": "0"             // result: 执行结果
}
```

**内存占用**: ~60字节/测点

#### 遥调YT（4个字段）

```
Key: ems:rtdb:yt:{point_code}
Hash: {
  "v": "50.0",         // value: 工程值 (double)
  "t": "1712001234",
  "q": "0",
  "s": "0"
}
```

**内存占用**: ~50字节/测点

## API接口

### 初始化

```cpp
RTDBConfig config;
config.host = "localhost";
config.port = 6379;
config.password = "888888";
config.pool_size = 10;
config.timeout_ms = 5000;

auto rtdb = createRTDB(config);
if (!rtdb->isConnected()) {
    std::cerr << "连接失败: " << rtdb->getLastError() << std::endl;
}
```

### 遥测YC操作

```cpp
// 写入遥测数据
YCPoint yc;
yc.value = 220.5;
yc.timestamp = std::time(nullptr);
yc.quality = 0;
yc.status = 0;
rtdb->setYC("voltage_a", yc);

// 读取遥测数据
YCPoint yc_read;
if (rtdb->getYC("voltage_a", yc_read)) {
    std::cout << "值: " << yc_read.value << std::endl;
}

// 批量读取（支持通配符）
auto points = rtdb->getYCPoints("voltage_*");

// 获取所有测点代码
auto codes = rtdb->getYCCodes("voltage_*");

// 删除测点
rtdb->deleteYC("voltage_a");
```

### 遥信YX操作

```cpp
YXPoint yx;
yx.value = 1;
yx.timestamp = std::time(nullptr);
yx.quality = 0;
yx.status = 0;
rtdb->setYX("breaker_001", yx);

YXPoint yx_read;
if (rtdb->getYX("breaker_001", yx_read)) {
    std::cout << "状态: " << yx_read.value << std::endl;
}

auto points = rtdb->getYXPoints("*");
```

### 遥控YK操作

```cpp
YKPoint yk;
yk.value = 1;
yk.command_type = 0;
yk.timestamp = std::time(nullptr);
yk.status = YKStatus::Idle;
yk.result = 0;
rtdb->setYK("yk_breaker_001", yk);

// 读取遥控状态
YKPoint yk_read;
if (rtdb->getYK("yk_breaker_001", yk_read)) {
    std::cout << "状态: " << static_cast<int>(yk_read.status) << std::endl;
}

// 更新遥控状态为执行中
yk_read.status = YKStatus::Executing;
rtdb->setYK("yk_breaker_001", yk_read);
```

### 遥调YT操作

```cpp
YTPoint yt;
yt.value = 50.0;
yt.timestamp = std::time(nullptr);
yt.quality = 0;
yt.status = 0;
rtdb->setYT("setpoint_001", yt);

YTPoint yt_read;
if (rtdb->getYT("setpoint_001", yt_read)) {
    std::cout << "设定值: " << yt_read.value << std::endl;
}
```

## 性能对比

| 方案 | 数据量/测点 | Redis内存占用 | 单次操作延迟 | 适用场景 |
|------|------------|--------------|------------|---------|
| 完整模型存储 | ~2KB | 高 | ~1-2ms | 不推荐 |
| **精简结构存储** | ~50-100B | **低** | **~0.5-1ms** | **推荐** |
| 共享内存 | ~16B | 中 | ~0.01ms | 超高性能场景 |

**估算**:
- 10000个遥测点: ~500KB-1MB
- 10000个遥信点: ~500KB-1MB
- 10000个遥控点: ~600KB-1.2MB
- 10000个遥调点: ~500KB-1MB
- **总计**: ~2-4MB（支持10万个测点）

## 使用场景

### 场景1: 数据采集

```cpp
void data_collection_thread() {
    auto rtdb = createRTDB(config);
    
    while (running) {
        for (const auto& channel : channels) {
            auto data = channel->read();
            
            for (const auto& yc_data : data.yc_points) {
                YCPoint yc;
                yc.value = yc_data.value;
                yc.timestamp = std::time(nullptr);
                yc.quality = yc_data.quality;
                yc.status = 0;
                rtdb->setYC(yc_data.code, yc);
            }
            
            for (const auto& yx_data : data.yx_points) {
                YXPoint yx;
                yx.value = yx_data.value;
                yx.timestamp = std::time(nullptr);
                yx.quality = yx_data.quality;
                yx.status = 0;
                rtdb->setYX(yx_data.code, yx);
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
```

### 场景2: Web API服务

```cpp
void handle_get_yc(http_request req, const std::string& code) {
    auto rtdb = createRTDB(config);
    
    YCPoint yc;
    if (rtdb->getYC(code, yc)) {
        json response = {
            {"code", code},
            {"value", yc.value},
            {"timestamp", yc.timestamp},
            {"quality", yc.quality},
            {"status", yc.status}
        };
        send_json_response(req, response);
    } else {
        send_error_response(req, 404, "Point not found");
    }
}
```

### 场景3: 告警检测

```cpp
void alarm_detection_thread() {
    auto rtdb = createRTDB(config);
    auto db = createDBConnection();
    
    while (running) {
        auto yc_codes = rtdb->getYCCodes("*");
        
        for (const auto& code : yc_codes) {
            YCPoint yc;
            if (rtdb->getYC(code, yc)) {
                auto point_config = db->getPointConfig(code);
                
                if (yc.value > point_config.max_limit || 
                    yc.value < point_config.min_limit) {
                    trigger_alarm(code, yc.value, point_config);
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
```

### 场景4: 遥控操作

```cpp
bool execute_yk(const std::string& code, int value) {
    auto rtdb = createRTDB(config);
    
    YKPoint yk;
    if (!rtdb->getYK(code, yk)) {
        return false;
    }
    
    if (yk.status != YKStatus::Idle) {
        return false;
    }
    
    yk.status = YKStatus::Pending;
    yk.value = value;
    yk.timestamp = std::time(nullptr);
    rtdb->setYK(code, yk);
    
    bool result = execute_command_on_device(code, value);
    
    yk.status = result ? YKStatus::Success : YKStatus::Failed;
    yk.result = result ? 0 : -1;
    yk.timestamp = std::time(nullptr);
    rtdb->setYK(code, yk);
    
    return result;
}
```

## Redis管理命令

### 查看所有测点

```bash
redis-cli
KEYS ems:rtdb:*
KEYS ems:rtdb:yc:*
KEYS ems:rtdb:yx:*
```

### 查看单个测点数据

```bash
HGETALL ems:rtdb:yc:voltage_a
```

### 统计测点数量

```bash
DBSIZE
KEYS ems:rtdb:yc:* | wc -l
```

### 批量删除测试数据

```bash
DEL $(KEYS "ems:rtdb:*")
```

### 监控实时操作

```bash
redis-cli MONITOR
```

## 性能优化建议

### 1. Pipeline批量操作

```cpp
void batch_update_yc(const std::vector<YCPoint>& points) {
    auto pipeline = redis_->pipeline();
    
    for (const auto& point : points) {
        std::string key = buildYcKey(point.code);
        pipeline.hset(key, "v", std::to_string(point.value));
        pipeline.hset(key, "t", std::to_string(point.timestamp));
        pipeline.hset(key, "q", std::to_string(point.quality));
        pipeline.hset(key, "s", std::to_string(point.status));
    }
    
    auto replies = pipeline.exec();
}
```

### 2. 连接池优化

```cpp
RTDBConfig config;
config.pool_size = 20;  // 根据并发量调整
config.timeout_ms = 3000;
```

### 3. 数据过期策略

```cpp
bool setYCWithExpire(const std::string& code, const YCPoint& point, int expire_seconds) {
    std::string key = buildYcKey(code);
    
    std::map<std::string, std::string> fields = {
        {"v", std::to_string(point.value)},
        {"t", std::to_string(point.timestamp)},
        {"q", std::to_string(point.quality)},
        {"s", std::to_string(point.status)}
    };
    
    auto pipeline = redis_->pipeline();
    pipeline.hset(key, fields);
    pipeline.expire(key, expire_seconds);
    pipeline.exec();
}
```

## 故障排除

### 连接失败

```cpp
if (!rtdb->isConnected()) {
    std::cerr << "Redis连接失败: " << rtdb->getLastError() << std::endl;
    
    // 检查Redis服务
    // systemctl status redis
    // redis-cli ping
}
```

### 数据不一致

```cpp
// 定期检查测点配置与实时数据的一致性
void check_consistency() {
    auto rtdb_codes = rtdb->getYCCodes("*");
    auto db_codes = db->getAllYCCodes();
    
    std::vector<std::string> diff;
    std::set_difference(db_codes.begin(), db_codes.end(),
                       rtdb_codes.begin(), rtdb_codes.end(),
                       std::back_inserter(diff));
    
    if (!diff.empty()) {
        std::cerr << "发现 " << diff.size() << " 个测点缺失实时数据" << std::endl;
    }
}
```

## 未来扩展

### 1. 共享内存层（可选）

对于超高性能要求的场景，可以添加第三层共享内存：

```cpp
struct SharedMemoryYC {
    double value;
    uint64_t timestamp;
    int quality;
    int status;
};
```

### 2. 数据压缩

对于历史数据，可以使用Redis的压缩功能：

```cpp
void compress_old_data(const std::string& key) {
    auto data = redis_->get(key);
    auto compressed = compress(data);
    redis_->set(key + ":compressed", compressed);
}
```

### 3. 集群支持

Redis集群可以水平扩展：

```cpp
auto cluster = std::make_unique<sw::redis::RedisCluster>(
    "tcp://127.0.0.1:7000", 
    "tcp://127.0.0.1:7001"
);
```

## 总结

本架构通过将测点配置数据与实时数据分离，实现了：

✅ **内存优化**: 每个测点仅占用50-100字节，相比完整模型减少95%以上

✅ **性能提升**: 单次操作延迟降低至0.5-1ms

✅ **可扩展性**: 支持10万+测点，内存占用仅需2-4GB

✅ **清晰分层**: 配置与数据分离，便于维护和扩展

✅ **灵活查询**: 支持多种查询模式（单个、批量、通配符）
