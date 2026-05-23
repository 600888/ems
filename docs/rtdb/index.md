# EMS 实时数据库技术文档

## 1. 概述

### 1.1 现状分析

当前EMS实时数据库存在以下问题：

| 问题类型 | 具体问题 | 影响 |
| :--- | :--- | :--- |
| 配置管理 | 点表配置依赖关系型数据库，缺乏灵活导入方式 | 部署复杂，难以批量导入点表 |
| 架构设计 | 组件职责不够清晰，缺乏统一抽象层 | 代码耦合度高，维护困难 |
| 性能优化 | 缺乏批量操作优化和内存映射机制 | 高并发场景下性能受限 |
| 高可用性 | 单点Redis依赖，无故障切换机制 | 单点故障导致系统不可用 |
| 扩展性 | 点表类型扩展困难，缺乏通用抽象 | 新增点类型需要大量修改 |

### 1.2 重构目标

| 目标 | 描述 |
| :--- | :--- |
| **灵活导入** | 支持Excel、JSON等多种格式导入通用点表 |
| **高性能** | 支持内存映射、批量操作、异步写入 |
| **高可用** | 支持Redis主从复制、哨兵模式、故障自动切换 |
| **现代C++** | 使用C++17特性，RAII、智能指针、现代并发模型 |
| **可扩展** | 插件化点表类型，支持动态扩展 |

---

## 2. 架构设计

### 2.1 整体架构

```
┌─────────────────────────────────────────────────────────────────────┐
│                        应用层 (Application)                          │
│   ModbusHandler │ IEC104Handler │ WebAPI │ 其他业务进程              │
├─────────────────────────────────────────────────────────────────────┤
│                        抽象层 (Abstraction)                         │
│           RtDbClient ───────────────────────────────────────┐      │
│           ┌───────────────────────────────────────────────┐ │      │
│           │  PointImporter  │  PointExporter  │ Watcher   │ │      │
│           └───────────────────────────────────────────────┘ │      │
├─────────────────────────────────────────────────────────────────────┤
│                        核心层 (Core)                               │
│           ┌──────────┐  ┌──────────┐  ┌──────────┐               │
│           │ YCPoint  │  │ YXPoint  │  │ YKPoint  │ ...           │
│           └────┬─────┘  └────┬─────┘  └────┬─────┘               │
│                │             │             │                       │
│           ┌────┴─────────────┴─────────────┴────┐                 │
│           │         PointManager (内存缓存)       │                 │
│           └───────────────────┬──────────────────┘                 │
├─────────────────────────────────────────────────────────────────────┤
│                        存储层 (Storage)                             │
│              ┌─────────────────────────────┐                       │
│              │        Redis Cluster         │                       │
│              │  Master ↔ Slave ↔ Sentinel   │                       │
│              └─────────────────────────────┘                       │
└─────────────────────────────────────────────────────────────────────┘
```

### 2.2 模块职责

| 模块 | 职责 | 关键特性 |
| :--- | :--- | :--- |
| **RtDbClient** | 统一API入口，提供同步/异步操作 | 连接池、自动重连、负载均衡 |
| **PointImporter** | 点表导入器，支持多格式 | Excel(.xlsx)、JSON、CSV |
| **PointExporter** | 点表导出器，支持多格式 | JSON、CSV、二进制快照 |
| **Watcher** | 数据变更监听，事件通知 | 订阅/发布模式、回调机制 |
| **PointManager** | 内存缓存管理器 | 读写分离、批量刷新、过期策略 |
| **StorageAdapter** | 存储适配层 | Redis、LevelDB可切换 |

### 2.3 核心设计模式

| 模式 | 应用场景 | 实现方式 |
| :--- | :--- | :--- |
| **Singleton** | RtDbClient实例管理 | 线程安全的懒汉模式 |
| **Factory** | 点类型创建 | 注册式工厂，支持扩展 |
| **Strategy** | 导入格式策略 | 策略接口+具体实现 |
| **Observer** | 数据变更通知 | 回调+事件队列 |
| **Cache-Aside** | 数据读写策略 | 先查缓存，再查存储 |

---

## 3. 数据模型

### 3.1 点类型体系

#### 3.1.1 基础点接口

```cpp
class BasePoint {
public:
    virtual ~BasePoint() = default;
    
    virtual std::string code() const = 0;
    virtual std::string name() const = 0;
    virtual PointType type() const = 0;
    virtual uint64_t timestamp() const = 0;
    virtual PointQuality quality() const = 0;
    
    virtual void update(const BasePoint& other) = 0;
    virtual std::string toJson() const = 0;
    virtual void fromJson(const std::string& json) = 0;
};
```

#### 3.1.2 点类型定义

| 类型 | 名称 | 数据类型 | 用途 |
| :--- | :--- | :--- | :--- |
| **YC** | 遥测 | double | 模拟量测量（电压、电流、功率等） |
| **YX** | 遥信 | bool/int | 状态量（开关位置、告警等） |
| **YK** | 遥控 | int | 控制命令（合闸、分闸等） |
| **YT** | 遥调 | double | 设定值调整（温度、压力等） |

#### 3.1.3 解析码（DecodeCode）

解析码定义测点原始数据在通信协议中的编码格式，用于协议解析时确定数据类型和字节长度。

```cpp
enum class DecodeCode {
    UINT16,      // 无符号16位整数（1个寄存器）
    INT16,       // 有符号16位整数（1个寄存器）
    UINT32,      // 无符号32位整数（2个寄存器）
    INT32,       // 有符号32位整数（2个寄存器）
    FLOAT,       // 单精度浮点数（2个寄存器，IEEE754）
    DOUBLE,      // 双精度浮点数（4个寄存器，IEEE754）
    UINT8,       // 无符号8位整数
    INT8,        // 有符号8位整数
    BOOL,        // 布尔值（0/1）
    BIT          // 位操作（指定bit偏移）
};
```

| 解析码 | 数据类型 | 字节长度 | Modbus寄存器数 | 说明 |
| :--- | :--- | :--- | :--- | :--- |
| **UINT16** | uint16_t | 2字节 | 1 | 无符号16位，范围0~65535 |
| **INT16** | int16_t | 2字节 | 1 | 有符号16位，范围-32768~32767 |
| **UINT32** | uint32_t | 4字节 | 2 | 无符号32位，范围0~4294967295 |
| **INT32** | int32_t | 4字节 | 2 | 有符号32位，范围-2147483648~2147483647 |
| **FLOAT** | float | 4字节 | 2 | 单精度浮点，IEEE754标准 |
| **DOUBLE** | double | 8字节 | 4 | 双精度浮点，IEEE754标准 |
| **UINT8** | uint8_t | 1字节 | - | 无符号8位 |
| **INT8** | int8_t | 1字节 | - | 有符号8位 |
| **BOOL** | bool | 1字节 | - | 布尔值，0或1 |
| **BIT** | bit | 1位 | - | 位操作，需配合bit偏移使用 |

> **默认值**: YC/YT测点默认解析码为 `UINT16`，YX/YK测点默认解析码为 `BOOL`。

#### 3.1.4 YC点数据结构

```cpp
struct YcPoint : public BasePoint {
    std::string code;           // 点编码
    std::string name;           // 点名称
    DecodeCode decode_code;     // 解析码（数据编码格式）
    double value;               // 测量值
    double raw_value;           // 原始值（未转换）
    double mul_coe;             // 乘法系数
    double add_coe;             // 加法系数
    double min_limit;           // 下限
    double max_limit;           // 上限
    std::string unit;           // 单位
    uint64_t timestamp;         // 时间戳
    PointQuality quality;       // 品质
    int status;                 // 状态
    
    double transformed_value() const {
        return raw_value * mul_coe + add_coe;
    }
};
```

#### 3.1.5 YX点数据结构

```cpp
struct YxPoint : public BasePoint {
    std::string code;           // 点编码
    std::string name;           // 点名称
    DecodeCode decode_code;     // 解析码（数据编码格式）
    bool value;                 // 状态值
    int bit;                    // 位偏移
    bool reverse;               // 取反标志
    uint64_t timestamp;         // 时间戳
    PointQuality quality;       // 品质
    int status;                 // 状态
};
```

#### 3.1.6 YK点数据结构

```cpp
struct YkPoint : public BasePoint {
    std::string code;           // 点编码
    std::string name;           // 点名称
    DecodeCode decode_code;     // 解析码（数据编码格式）
    int value;                  // 命令值
    int command_type;           // 命令类型
    YKStatus status;            // 执行状态
    uint64_t timestamp;         // 时间戳
    uint64_t execute_time;      // 执行时间
    std::string result;         // 执行结果
};
```

#### 3.1.7 YT点数据结构

```cpp
struct YtPoint : public BasePoint {
    std::string code;           // 点编码
    std::string name;           // 点名称
    DecodeCode decode_code;     // 解析码（数据编码格式）
    double value;               // 设定值
    double mul_coe;             // 乘法系数
    double add_coe;             // 加法系数
    double min_limit;           // 下限
    double max_limit;           // 上限
    std::string unit;           // 单位
    uint64_t timestamp;         // 时间戳
    PointQuality quality;       // 品质
};
```

### 3.2 配置数据结构

#### 3.2.1 点表配置

```cpp
struct PointConfig {
    std::string code;                       // 点编码（唯一标识）
    std::string name;                       // 点名称
    PointType point_type;                   // 点类型
    int64_t channel_id;                     // 通道ID
    int64_t device_id;                      // 设备ID
    DecodeCode decode_code{DecodeCode::UINT16}; // 解析码（数据编码格式）
    
    // YC/YT 专用
    double mul_coe{1.0};                    // 乘法系数
    double add_coe{0.0};                    // 加法系数
    double min_limit{-9999999.0};           // 下限
    double max_limit{9999999.0};            // 上限
    std::string unit;                       // 单位
    
    // YX/YK 专用
    int bit{0};                             // 位偏移
    bool reverse{false};                    // 取反标志
    int command_type{0};                    // 命令类型
    
    // 通用属性
    std::string description;                // 描述
    int status{1};                          // 启用状态
    std::string created_at;                 // 创建时间
    std::string updated_at;                 // 更新时间
};
```

#### 3.2.2 通道配置

```cpp
struct ChannelConfig {
    int64_t id;                             // 通道ID
    std::string name;                       // 通道名称
    std::string protocol;                   // 协议类型（modbus/iec104等）
    std::string connection;                 // 连接串
    std::string settings;                   // 配置参数
    int baud_rate{9600};                    // 波特率
    int data_bits{8};                       // 数据位
    int stop_bits{1};                       // 停止位
    std::string parity{"none"};             // 校验位
    int status{1};                          // 启用状态
};
```

---

## 4. API接口设计

### 4.1 RtDbClient 接口

#### 4.1.1 初始化与配置

```cpp
class RtDbClient {
public:
    // 单例获取
    static std::shared_ptr<RtDbClient> instance();
    
    // 初始化
    bool initialize(const RtDbConfig& config);
    void shutdown();
    bool isConnected() const;
    
    // 配置管理
    bool loadConfigFromJson(const std::string& filepath);
    bool loadConfigFromExcel(const std::string& filepath);
    bool loadConfigFromCsv(const std::string& filepath);
    bool saveConfigToJson(const std::string& filepath) const;
    
    // 连接状态
    std::string getLastError() const;
    int getConnectionPoolSize() const;
};
```

#### 4.1.2 点数据操作（同步）

```cpp
class RtDbClient {
public:
    // 单点读写
    bool setPoint(const BasePoint& point);
    bool getPoint(const std::string& code, std::unique_ptr<BasePoint>& point);
    bool deletePoint(const std::string& code);
    bool existsPoint(const std::string& code);
    
    // 批量操作
    bool batchSetPoints(const std::vector<std::unique_ptr<BasePoint>>& points);
    bool batchGetPoints(const std::vector<std::string>& codes, 
                        std::vector<std::unique_ptr<BasePoint>>& points);
    bool batchDeletePoints(const std::vector<std::string>& codes);
    
    // 按类型查询
    std::vector<std::unique_ptr<BasePoint>> getAllPoints(PointType type);
    std::vector<std::string> getAllPointCodes(PointType type);
    size_t getPointCount(PointType type);
};
```

#### 4.1.3 点数据操作（异步）

```cpp
class RtDbClient {
public:
    // 异步单点操作
    std::future<bool> asyncSetPoint(const BasePoint& point);
    std::future<std::unique_ptr<BasePoint>> asyncGetPoint(const std::string& code);
    std::future<bool> asyncDeletePoint(const std::string& code);
    
    // 异步批量操作
    std::future<bool> asyncBatchSetPoints(const std::vector<std::unique_ptr<BasePoint>>& points);
    std::future<std::vector<std::unique_ptr<BasePoint>>> asyncBatchGetPoints(
        const std::vector<std::string>& codes);
    
    // 回调模式
    using PointCallback = std::function<void(bool, const std::string&)>;
    void setPointAsync(const BasePoint& point, PointCallback callback);
};
```

#### 4.1.4 配置缓存操作

```cpp
class RtDbClient {
public:
    // 配置缓存管理
    bool loadConfigCache();
    bool reloadConfigCache();
    void clearConfigCache();
    
    // 配置查询
    bool getPointConfig(const std::string& code, PointConfig& config);
    bool getChannelConfig(int64_t channel_id, ChannelConfig& config);
    
    // 配置更新
    bool updatePointConfig(const PointConfig& config);
    bool updateChannelConfig(const ChannelConfig& config);
};
```

### 4.2 Watcher 接口

```cpp
class Watcher {
public:
    using DataChangeCallback = std::function<void(
        PointType type,
        const std::string& code,
        const BasePoint& old_value,
        const BasePoint& new_value
    )>;
    
    using BatchChangeCallback = std::function<void(
        const std::vector<std::pair<std::string, BasePoint>>& changes
    )>;
    
    // 订阅单点变更
    bool subscribe(const std::string& point_code, DataChangeCallback callback);
    bool unsubscribe(const std::string& point_code);
    
    // 订阅类型变更
    bool subscribeType(PointType type, DataChangeCallback callback);
    bool unsubscribeType(PointType type);
    
    // 批量订阅
    bool subscribePattern(const std::string& pattern, DataChangeCallback callback);
    
    // 批量变更监听
    void setBatchCallback(BatchChangeCallback callback);
    void setBatchInterval(std::chrono::milliseconds interval);
    
    // 启动/停止监听
    void start();
    void stop();
};
```

### 4.3 PointImporter 接口

```cpp
class PointImporter {
public:
    enum class Format {
        JSON,
        Excel,
        CSV,
        Binary
    };
    
    // 导入点表配置
    bool import(const std::string& filepath, Format format);
    bool importFromString(const std::string& content, Format format);
    
    // 导入结果
    struct ImportResult {
        size_t total_count{0};
        size_t success_count{0};
        size_t fail_count{0};
        std::vector<std::string> errors;
    };
    
    const ImportResult& getLastResult() const;
    
    // 验证导入数据
    bool validate(const std::string& filepath, Format format);
    bool validateFromString(const std::string& content, Format format);
    
    // 预览导入数据（不实际写入）
    std::vector<PointConfig> preview(const std::string& filepath, Format format);
};
```

---

## 5. 点表导入方案

### 5.1 支持的导入格式

#### 5.1.1 Excel格式 (.xlsx)

**文件结构**:
- 一个Excel文件包含多个Sheet，每个Sheet对应一种点类型
- Sheet名称固定为：`YC`, `YX`, `YK`, `YT`, `Channel`

**YC Sheet示例**:

| code | name | channel_id | decode_code | mul_coe | add_coe | min_limit | max_limit | unit | description |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| YC_001 | 母线电压 | 1 | UINT16 | 1.0 | 0.0 | 0.0 | 1000.0 | V | 1号母线电压测量 |
| YC_002 | 有功功率 | 1 | INT16 | 0.1 | 0.0 | -1000.0 | 1000.0 | MW | 有功功率测量 |

**YX Sheet示例**:

| code | name | channel_id | decode_code | bit | reverse | description |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| YX_001 | 断路器位置 | 1 | BOOL | 0 | false | 断路器合闸状态 |
| YX_002 | 保护动作 | 1 | BIT | 1 | false | 保护装置动作信号 |

#### 5.1.2 JSON格式

```json
{
    "version": "1.0",
    "import_time": "2024-01-01 12:00:00",
    "channels": [
        {
            "id": 1,
            "name": "主站通道",
            "protocol": "modbus-rtu",
            "connection": "/dev/ttyS0",
            "settings": "baud=9600,parity=none",
            "status": 1
        }
    ],
    "yc_points": [
        {
            "code": "YC_001",
            "name": "母线电压",
            "channel_id": 1,
            "decode_code": "UINT16",
            "mul_coe": 1.0,
            "add_coe": 0.0,
            "min_limit": 0.0,
            "max_limit": 1000.0,
            "unit": "V",
            "description": "1号母线电压测量",
            "status": 1
        }
    ],
    "yx_points": [
        {
            "code": "YX_001",
            "name": "断路器位置",
            "channel_id": 1,
            "decode_code": "BOOL",
            "bit": 0,
            "reverse": false,
            "description": "断路器合闸状态",
            "status": 1
        }
    ],
    "yk_points": [],
    "yt_points": []
}
```

#### 5.1.3 CSV格式

```csv
code,name,type,channel_id,decode_code,mul_coe,add_coe,min_limit,max_limit,unit,bit,reverse,description,status
YC_001,母线电压,YC,1,UINT16,1.0,0.0,0.0,1000.0,V,,,1号母线电压测量,1
YX_001,断路器位置,YX,1,BOOL,,,0,false,断路器合闸状态,1
YK_001,合闸命令,YK,1,BOOL,,,,1,合闸控制命令,1
YT_001,温度设定,YT,1,FLOAT,1.0,0.0,-40.0,80.0,C,,,温度设定值,1
```

### 5.2 导入流程

```
┌──────────────────────────────────────────────────────────────┐
│                      导入流程                                │
├──────────────────────────────────────────────────────────────┤
│  1. 读取文件                                                 │
│     ↓                                                       │
│  2. 格式解析 (Excel/JSON/CSV)                                │
│     ↓                                                       │
│  3. 数据验证                                                 │
│     ├─ 编码唯一性检查                                         │
│     ├─ 通道存在性检查                                         │
│     └─ 参数合法性检查                                         │
│     ↓                                                       │
│  4. 事务写入                                                 │
│     ├─ 写入配置缓存                                           │
│     ├─ 写入Redis存储                                          │
│     └─ 更新索引                                               │
│     ↓                                                       │
│  5. 通知变更                                                 │
│     └─ 触发Watcher事件                                       │
└──────────────────────────────────────────────────────────────┘
```

### 5.3 导入API使用示例

```cpp
// 创建导入器
auto importer = std::make_shared<PointImporter>();

// 导入Excel文件
bool success = importer->import("/path/to/points.xlsx", PointImporter::Format::Excel);

// 获取导入结果
const auto& result = importer->getLastResult();
std::cout << "导入总数: " << result.total_count << std::endl;
std::cout << "成功: " << result.success_count << std::endl;
std::cout << "失败: " << result.fail_count << std::endl;

// 打印错误信息
for (const auto& err : result.errors) {
    std::cerr << "错误: " << err << std::endl;
}

// 导入JSON字符串
std::string json_content = R"(
{
    "yc_points": [...],
    "yx_points": [...]
}
)";
success = importer->importFromString(json_content, PointImporter::Format::JSON);
```

---

## 6. 高性能设计

### 6.1 内存映射

#### 6.1.1 内存缓存架构

```
┌─────────────────────────────────────────────────────────────┐
│                    PointManager                            │
├─────────────────────────────────────────────────────────────┤
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │   YC Cache   │  │   YX Cache   │  │   YK Cache   │ ... │
│  │ (Concurrent) │  │ (Concurrent) │  │ (Concurrent) │      │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘      │
│         │                 │                 │               │
│         └────────┬────────┴────────┬────────┘               │
│                  ↓                 ↓                        │
│         ┌──────────────┐  ┌──────────────┐                  │
│         │  写缓冲队列   │  │  过期策略    │                  │
│         └──────────────┘  └──────────────┘                  │
└─────────────────────────────────────────────────────────────┘
```

#### 6.1.2 缓存策略

| 策略 | 描述 | 实现 |
| :--- | :--- | :--- |
| **读写分离** | 读操作优先访问内存，写操作同时更新内存和存储 | 双缓冲设计 |
| **批量刷新** | 累积一定数量后批量写入Redis | 阈值触发+定时触发 |
| **过期淘汰** | LRU/LFU策略清理冷数据 | 定时扫描+引用计数 |
| **分片存储** | 按点类型或哈希分片 | 减少锁竞争 |

### 6.2 批量操作优化

#### 6.2.1 Pipeline模式

```cpp
class RtDbClient {
public:
    // Pipeline批量写入
    bool pipelineSetPoints(const std::vector<std::unique_ptr<BasePoint>>& points) {
        auto pipeline = redis_->pipeline();
        
        for (const auto& point : points) {
            std::string key = buildKey(point->code());
            std::string value = point->toJson();
            pipeline.set(key, value);
            
            // 每1000个点刷新一次
            if (++count % 1000 == 0) {
                pipeline.exec();
                pipeline = redis_->pipeline();
            }
        }
        
        if (count % 1000 != 0) {
            pipeline.exec();
        }
        
        return true;
    }
};
```

#### 6.2.2 批量查询优化

```cpp
class RtDbClient {
public:
    std::vector<std::unique_ptr<BasePoint>> batchGetPointsOptimized(
        const std::vector<std::string>& codes) {
        
        std::vector<std::unique_ptr<BasePoint>> results;
        results.reserve(codes.size());
        
        // 先查缓存
        std::vector<std::string> miss_codes;
        
        for (const auto& code : codes) {
            if (auto point = cache_.get(code)) {
                results.push_back(std::move(point));
            } else {
                miss_codes.push_back(code);
            }
        }
        
        // 批量查询缓存未命中的数据
        if (!miss_codes.empty()) {
            auto redis_results = redis_->mget(miss_codes);
            
            for (size_t i = 0; i < miss_codes.size(); ++i) {
                if (redis_results[i]) {
                    auto point = deserializePoint(redis_results[i].value());
                    cache_.set(miss_codes[i], point);
                    results.push_back(std::move(point));
                }
            }
        }
        
        return results;
    }
};
```

### 6.3 异步写入

```cpp
class AsyncWriter {
public:
    AsyncWriter(size_t queue_size = 10000) 
        : running_(true), 
          thread_(&AsyncWriter::worker, this),
          queue_(queue_size) {}
    
    ~AsyncWriter() {
        running_ = false;
        cv_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    
    void write(std::unique_ptr<BasePoint> point) {
        queue_.push(std::move(point));
        cv_.notify_one();
    }
    
    void writeBatch(std::vector<std::unique_ptr<BasePoint>> points) {
        for (auto& point : points) {
            queue_.push(std::move(point));
        }
        cv_.notify_one();
    }
    
private:
    void worker() {
        while (running_) {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { 
                return !queue_.empty() || !running_; 
            });
            
            std::vector<std::unique_ptr<BasePoint>> batch;
            queue_.drain_to(batch);
            
            lock.unlock();
            
            if (!batch.empty()) {
                // 批量写入Redis
                writeToRedis(batch);
                // 更新内存缓存
                updateCache(batch);
            }
        }
    }
    
    void writeToRedis(const std::vector<std::unique_ptr<BasePoint>>& batch);
    void updateCache(const std::vector<std::unique_ptr<BasePoint>>& batch);
    
    std::atomic<bool> running_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    moodycamel::ConcurrentQueue<std::unique_ptr<BasePoint>> queue_;
};
```

---

## 7. 高可用设计

### 7.1 Redis主从架构

```
┌─────────────────────────────────────────────────────────────┐
│                      客户端层                               │
│              ┌─────────┬─────────┬─────────┐               │
│              │ Client1 │ Client2 │ Client3 │               │
│              └────┬────┴────┬────┴────┬────┘               │
│                   │         │         │                     │
├─────────────────────────────────────────────────────────────┤
│                      代理层 (可选)                          │
│                    ┌───────────┐                           │
│                    │  Sentinel │                           │
│                    └─────┬─────┘                           │
│                          │                                 │
├─────────────────────────────────────────────────────────────┤
│                      存储层                                 │
│         ┌───────────────┴───────────────┐                  │
│         ↓                               ↓                  │
│    ┌──────────┐                  ┌──────────┐              │
│    │  Master  │ ←──── 复制 ────→ │  Slave1  │              │
│    │ (读写)   │                  │ (只读)   │              │
│    └──────────┘                  └──────────┘              │
│                                      ↓                     │
│                                ┌──────────┐                │
│                                │  Slave2  │                │
│                                │ (只读)   │                │
│                                └──────────┘                │
└─────────────────────────────────────────────────────────────┘
```

### 7.2 故障切换流程

```
┌─────────────────────────────────────────────────────────────┐
│                    故障检测与切换                            │
├─────────────────────────────────────────────────────────────┤
│  1. Sentinel检测Master故障                                   │
│     ↓                                                       │
│  2. 选举新Master (Slave1)                                    │
│     ↓                                                       │
│  3. 通知所有Slave指向新Master                                 │
│     ↓                                                       │
│  4. 通知客户端更新连接信息                                     │
│     ↓                                                       │
│  5. 原Master恢复后作为Slave加入集群                            │
└─────────────────────────────────────────────────────────────┘
```

### 7.3 客户端连接池

```cpp
class ConnectionPool {
public:
    struct Connection {
        std::shared_ptr<sw::redis::Redis> client;
        std::string host;
        int port;
        bool is_master;
        std::atomic<bool> in_use{false};
        std::chrono::steady_clock::time_point last_used;
    };
    
    // 获取可用连接（优先从库读）
    std::shared_ptr<Connection> getConnection(bool prefer_slave = true);
    
    // 获取主库连接（写操作）
    std::shared_ptr<Connection> getMasterConnection();
    
    // 释放连接
    void releaseConnection(std::shared_ptr<Connection> conn);
    
    // 健康检查
    void healthCheck();
    
    // 故障切换时更新连接列表
    void updateConnections(const std::vector<ConnectionInfo>& info);
    
private:
    std::vector<std::shared_ptr<Connection>> connections_;
    std::atomic<size_t> master_index_{0};
    std::mutex mutex_;
    std::chrono::seconds max_idle_time_{300};
};
```

### 7.4 数据持久化策略

| 策略 | 描述 | 适用场景 |
| :--- | :--- | :--- |
| **RDB快照** | 定期全量快照 | 备份、灾难恢复 |
| **AOF日志** | 记录所有写操作 | 数据完整性要求高 |
| **混合模式** | RDB + AOF | 兼顾性能和可靠性 |

---

## 8. 部署与集成

### 8.1 依赖组件

| 组件 | 版本 | 用途 |
| :--- | :--- | :--- |
| **Redis** | >= 6.2 | 主存储 |
| **Redis Sentinel** | >= 6.2 | 高可用 |
| **nlohmann/json** | >= 3.0 | JSON解析 |
| **rapidcsv** | >= 8.0 | CSV解析 |
| **libxl** | >= 4.0 | Excel解析 |
| **redis-plus-plus** | >= 1.3 | Redis客户端 |

### 8.2 编译配置

```cmake
# CMakeLists.txt

# 依赖库
find_package(Redis++ REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)

# 编译选项
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 库编译
add_library(rtdb 
    src/rtdb_client.cpp
    src/point_manager.cpp
    src/point_importer.cpp
    src/watcher.cpp
    src/async_writer.cpp
    src/connection_pool.cpp
)

# 链接依赖
target_link_libraries(rtdb
    PUBLIC
        sw::redis++
        nlohmann_json::nlohmann_json
        Threads::Threads
    PRIVATE
        ${LIBXL_LIBRARIES}
)
```

### 8.3 配置文件示例

```json
{
    "redis": {
        "master": {
            "host": "127.0.0.1",
            "port": 6379,
            "password": "",
            "db": 0
        },
        "slaves": [
            {"host": "127.0.0.1", "port": 6380},
            {"host": "127.0.0.1", "port": 6381}
        ],
        "sentinel": {
            "host": "127.0.0.1",
            "port": 26379,
            "master_name": "mymaster"
        },
        "pool_size": 10,
        "timeout_ms": 5000,
        "retry_count": 3,
        "retry_delay_ms": 1000
    },
    "cache": {
        "enabled": true,
        "max_size": 100000,
        "expire_seconds": 300,
        "flush_interval_ms": 1000
    },
    "watcher": {
        "enabled": true,
        "batch_interval_ms": 100,
        "max_batch_size": 100
    },
    "logging": {
        "level": "info",
        "file": "/var/log/rtdb.log",
        "max_size_mb": 100,
        "max_files": 5
    }
}
```

---

## 9. 代码安全性

### 9.1 注意事项

| 风险类型 | 风险描述 | 关联模块 |
| :--- | :--- | :--- |
| **注入攻击** | 点编码可能包含恶意字符 | PointImporter, RtDbClient |
| **越权访问** | 未授权访问敏感点数据 | RtDbClient |
| **数据篡改** | 恶意修改点配置 | PointImporter |
| **拒绝服务** | 大量请求导致服务不可用 | ConnectionPool |
| **信息泄露** | 错误信息暴露系统细节 | 全局错误处理 |

### 9.2 解决方案

| 风险类型 | 解决方案 | 实施位置 |
| :--- | :--- | :--- |
| **注入攻击** | 对输入进行严格校验和转义，使用参数化查询 | PointImporter::validate(), RtDbClient |
| **越权访问** | 增加访问控制层，验证请求来源合法性 | RtDbClient, API网关 |
| **数据篡改** | 导入前备份、导入后校验、支持回滚 | PointImporter |
| **拒绝服务** | 连接池限流、请求频率限制、熔断机制 | ConnectionPool |
| **信息泄露** | 统一错误处理，不暴露内部细节 | 全局异常处理 |

### 9.3 输入验证示例

```cpp
bool validatePointCode(const std::string& code) {
    // 只允许字母、数字、下划线
    if (code.empty() || code.length() > 64) {
        return false;
    }
    
    for (char c : code) {
        if (!std::isalnum(c) && c != '_') {
            return false;
        }
    }
    
    // 不允许以数字开头
    if (std::isdigit(code[0])) {
        return false;
    }
    
    return true;
}

bool validatePointConfig(const PointConfig& config) {
    if (!validatePointCode(config.code)) {
        return false;
    }
    
    if (config.name.empty() || config.name.length() > 128) {
        return false;
    }
    
    // YC/YT 范围校验
    if (config.point_type == PointType::YC || config.point_type == PointType::YT) {
        if (config.min_limit >= config.max_limit) {
            return false;
        }
    }
    
    return true;
}
```

---

## 10. 总结

### 10.1 重构收益

| 维度 | 重构前 | 重构后 | 收益 |
| :--- | :--- | :--- | :--- |
| **配置管理** | 依赖数据库 | 支持Excel/JSON/CSV导入 | 部署效率提升 |
| **性能** | 直接Redis操作 | 内存缓存+批量操作 | 吞吐量提升10倍+ |
| **可用性** | 单点Redis | 主从+哨兵 | 99.99%可用性 |
| **扩展性** | 硬编码点类型 | 插件化设计 | 新增类型无需修改核心 |
| **可维护性** | 代码耦合度高 | 职责清晰、接口统一 | 维护成本降低 |

### 10.2 实施建议

1. **第一阶段**：完成核心接口和内存缓存层
2. **第二阶段**：实现点表导入器和Watcher
3. **第三阶段**：完成高可用部署和异步写入
4. **第四阶段**：性能测试和优化

### 10.3 后续规划

- 支持时序数据库集成（InfluxDB、TimescaleDB）
- 增加数据压缩和增量同步
- 提供gRPC接口支持跨语言调用
- 实现分布式锁和事务支持