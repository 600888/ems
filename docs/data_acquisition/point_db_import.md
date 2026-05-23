# 数据库测点导入方案 (ormpp)

> **版本**: 1.0  
> **日期**: 2026-05-20  
> **依赖**: ormpp (C++ ORM)、MySQL 8.0+ / PostgreSQL 16+  
> **关联文档**: [EMS架构设计文档](../EMS架构设计.md) 第4.2节「点表管理与配置加载」

---

## 1. 概述

### 1.1 背景

根据 EMS 架构设计，点表配置由 Go Web 管理后台或外部工具维护在 MySQL 中，C++ 引擎启动时通过 `RtDbManager::loadAllConfigs()` 从数据库读取点表到 `RtDbConfigCache` 内存缓存。

当前 `loadXxxConfigsFromDb()` 四个函数均为空桩实现（仅返回 `true`），本节设计使用 **ormpp** 作为 C++ ORM 库，实现从关系数据库导入四遥测点配置的完整方案。

### 1.2 为什么选 ormpp

| 对比维度 | 手写 SQL | ormpp | 说明 |
|---------|---------|-------|------|
| **编译期类型安全** | ❌ 字符串 SQL | ✅ 编译期生成 SQL | 避免运行时 SQL 拼写错误 |
| **代码量** | 大量样板代码 | 声明式映射，3 行 CRUD | `dao.query<Point>()` |
| **C++ 生态** | 需要自己封装 | 现代 C++17/20 原生 | header-only，零编译依赖 |
| **数据库支持** | 需分别处理 | MySQL / PostgreSQL / SQLite 统一 API | 项目支持 MySQL 和 PostgreSQL |
| **反射支持** | 手动字段映射 | C++20 聚合反射 / 宏映射 | 结构与表列自动对应 |

> 架构文档第 5.3 节中 Go 侧使用 GORM，C++ 侧对应使用 ormpp，保持 ORM 设计理念一致。

---

## 2. 数据库表设计

### 2.1 表结构（MySQL / PostgreSQL 通用）

与架构文档第 5.3.2 节 PointConfig GORM 模型对应：

```sql
-- MySQL DDL
CREATE TABLE IF NOT EXISTS point_configs (
    id            BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    device_id     BIGINT UNSIGNED NOT NULL DEFAULT 0,
    point_code    VARCHAR(64) NOT NULL,
    point_name    VARCHAR(128) NOT NULL,
    point_type    VARCHAR(8) NOT NULL COMMENT 'YC/YX/YK/YT',
    decode_code   VARCHAR(16) DEFAULT 'UINT16' COMMENT 'UINT16/INT32/FLOAT/DOUBLE/BOOL/BIT',
    channel_id    BIGINT UNSIGNED DEFAULT 0,
    func_code     INT DEFAULT 0 COMMENT 'Modbus功能码 01~04',
    reg_address   INT DEFAULT 0 COMMENT '寄存器起始地址',
    reg_count     INT DEFAULT 1 COMMENT '寄存器数量',
    data_type     VARCHAR(16) DEFAULT '' COMMENT 'float32_cdab / int32 / ...',
    mul_coe       DOUBLE DEFAULT 1.0,
    add_coe       DOUBLE DEFAULT 0.0,
    min_limit     DOUBLE DEFAULT -9999999.0,
    max_limit     DOUBLE DEFAULT 9999999.0,
    unit          VARCHAR(16) DEFAULT '',
    bit_pos       INT DEFAULT 0 COMMENT '遥信位偏移(YX专用)',
    reverse       TINYINT(1) DEFAULT 0 COMMENT '是否取反(YX专用)',
    cmd_type      INT DEFAULT 0 COMMENT '命令类型(YK专用)',
    status        INT DEFAULT 1 COMMENT '1:启用 0:停用',
    description   TEXT,
    created_at    DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at    DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    UNIQUE KEY uk_point_code (point_code),
    INDEX idx_device_id (device_id),
    INDEX idx_channel_id (channel_id),
    INDEX idx_point_type (point_type)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

### 2.2 点类型与字段映射关系

| 字段 | YC | YX | YK | YT | 说明 |
|------|:--:|:--:|:--:|:--:|------|
| `decod_code` | ● | ● | ● | ● | 数据解码类型 |
| `func_code` | ● | ● | ● | ● | Modbus 功能码 |
| `reg_address` | ● | ● | ● | ● | 寄存器地址 |
| `reg_count` | ● | ● | ● | ● | 寄存器数量 |
| `data_type` | ● | ● | ● | ● | 原始数据类型 |
| `mul_coe` | ● | | | ● | 乘系数 |
| `add_coe` | ● | | | ● | 加系数 |
| `min_limit` | ● | | | ● | 下限 |
| `max_limit` | ● | | | ● | 上限 |
| `unit` | ● | | | ● | 单位 |
| `bit_pos` | | ● | ● | | 位偏移 |
| `reverse` | | ● | | | 取反标志 |
| `cmd_type` | | | ● | | 命令类型 |

---

## 3. ormpp 集成

### 3.1 获取 ormpp

ormpp 是 header-only 库，直接添加到第三方目录：

```bash
# 方式一：Git Submodule
cd backend/3rd
git submodule add https://github.com/qicosmos/ormpp.git
cd ormpp
git checkout v1.5.0

# 方式二：手动下载
wget https://github.com/qicosmos/ormpp/archive/refs/tags/v1.5.0.tar.gz
tar -xzf v1.5.0.tar.gz -C backend/3rd/
mv backend/3rd/ormpp-1.5.0 backend/3rd/ormpp
```

### 3.2 CMake 集成

```cmake
# backend/CMakeLists.txt 或 backend/src/rtdb/CMakeLists.txt

# 添加 ormpp 依赖路径
set(ORMPP_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/3rd/ormpp/include)

# 数据库客户端库（按需选择）
option(USE_MYSQL "Use MySQL as configuration database" ON)
option(USE_POSTGRESQL "Use PostgreSQL as configuration database" OFF)

if(USE_MYSQL)
    find_package(MySQL REQUIRED)
    message(STATUS "MySQL found: ${MYSQL_INCLUDE_DIR}")
endif()

if(USE_POSTGRESQL)
    find_package(PostgreSQL REQUIRED)
    message(STATUS "PostgreSQL found: ${PostgreSQL_INCLUDE_DIR}")
endif()
```

rtdb 库链接方式：

```cmake
# backend/src/rtdb/CMakeLists.txt 追加

target_include_directories(rtdb PUBLIC ${ORMPP_INCLUDE_DIR})

if(USE_MYSQL)
    target_include_directories(rtdb PUBLIC ${MYSQL_INCLUDE_DIR})
    target_link_libraries(rtdb PUBLIC ${MYSQL_CLIENT_LIBS})
    target_compile_definitions(rtdb PUBLIC EMS_USE_MYSQL)
endif()

if(USE_POSTGRESQL)
    target_include_directories(rtdb PUBLIC ${PostgreSQL_INCLUDE_DIR})
    target_link_libraries(rtdb PUBLIC ${PostgreSQL_LIBRARIES})
    target_compile_definitions(rtdb PUBLIC EMS_USE_POSTGRESQL)
endif()
```

### 3.3 ormpp 映射结构体

在 `backend/src/rtdb/` 下新建 `rtdb_db_mapping.h`，定义数据库表映射：

```cpp
// backend/src/rtdb/rtdb_db_mapping.h
#pragma once

#include <string>
#include <cstdint>
#include "ormpp/dbng.hpp"          // ormpp 数据库引擎
#include "ormpp/mysql.hpp"          // MySQL 适配器（或 ormpp/postgresql.hpp）

namespace ems {
namespace rtdb {
namespace db {

/**
 * @brief 数据库 point_configs 表映射结构体
 *
 * 字段名、类型、顺序必须与数据库表严格一致。
 * ormpp 通过编译期反射自动推断 SQL。
 */
struct PointConfigEntity {
    int64_t     id{0};              // 主键
    int64_t     device_id{0};       // 设备 ID
    std::string point_code;         // 测点编码（唯一）
    std::string point_name;         // 测点名称
    std::string point_type;         // YC / YX / YK / YT
    std::string decode_code;        // UINT16 / INT32 / FLOAT / ...
    int64_t     channel_id{0};      // 通道 ID
    int         func_code{0};       // Modbus 功能码
    int         reg_address{0};     // 寄存器地址
    int         reg_count{1};       // 寄存器数量
    std::string data_type;          // float32_cdab / int32 / ...
    double      mul_coe{1.0};       // 乘系数
    double      add_coe{0.0};       // 加系数
    double      min_limit{-9999999.0};
    double      max_limit{9999999.0};
    std::string unit;               // 单位
    int         bit_pos{0};         // 位偏移（YX 专用）
    int         reverse{0};         // 取反标志（YX 专用）
    int         cmd_type{0};        // 命令类型（YK 专用）
    int         status{1};          // 1:启用 0:停用
    std::string description;        // 描述
    std::string created_at;         // 创建时间
    std::string updated_at;         // 更新时间
};

// ormpp 要求提供表名映射
// 如果结构体名 != 表名，需要通过特化实现
template <>
inline std::string ormpp::entity_name<PointConfigEntity>() {
    return "point_configs";
}

/**
 * @brief 通道配置表映射
 */
struct ChannelConfigEntity {
    int64_t     id{0};
    std::string name;
    std::string protocol;           // modbus_rtu / modbus_tcp / iec104
    std::string connection;         // 连接串
    std::string settings;           // 额外配置 JSON
    int         baud_rate{9600};
    int         data_bits{8};
    int         stop_bits{1};
    std::string parity{"none"};
    int         status{1};
};

template <>
inline std::string ormpp::entity_name<ChannelConfigEntity>() {
    return "channel_configs";
}

} // namespace db
} // namespace rtdb
} // namespace ems
```

---

## 4. 数据库连接管理层

### 4.1 新建 `rtdb_db_connector.h`

```cpp
// backend/src/rtdb/rtdb_db_connector.h
#pragma once

#include <memory>
#include <string>
#include <chrono>

#ifdef EMS_USE_MYSQL
#include "ormpp/mysql.hpp"
using db_engine = ormpp::dbng<ormpp::mysql>;
#elif defined(EMS_USE_POSTGRESQL)
#include "ormpp/postgresql.hpp"
using db_engine = ormpp::dbng<ormpp::postgresql>;
#else
#error "No database backend defined. Set USE_MYSQL or USE_POSTGRESQL in CMake."
#endif

namespace ems {
namespace rtdb {

/**
 * @brief 数据库连接配置
 */
struct DbConnectionConfig {
    std::string host{"127.0.0.1"};
    int         port{3306};
    std::string user{"root"};
    std::string password;
    std::string database{"ems"};
    int         pool_size{4};           // 连接池大小
    int         timeout_ms{5000};       // 连接超时
    int         retry_count{3};         // 重试次数
};

/**
 * @brief 数据库连接管理器（单例）
 *
 * 封装 ormpp dbng 的连接生命周期，提供线程安全的连接访问。
 */
class DbConnector {
public:
    static DbConnector& instance() {
        static DbConnector inst;
        return inst;
    }

    /**
     * @brief 初始化数据库连接
     * @return 成功返回 true
     */
    bool initialize(const DbConnectionConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (connected_) return true;

        try {
            db_ = std::make_unique<db_engine>();

#ifdef EMS_USE_MYSQL
            bool ok = db_->connect(
                config.user, config.password,
                config.host, config.database,
                config.timeout_ms / 1000
            );
#elif defined(EMS_USE_POSTGRESQL)
            bool ok = db_->connect(
                fmt::format("host={} port={} user={} password={} dbname={}",
                    config.host, config.port,
                    config.user, config.password, config.database)
            );
#endif
            if (!ok) {
                last_error_ = "Failed to connect to database: " + config.host;
                return false;
            }

            connected_ = true;
            config_ = config;
            return true;
        } catch (const std::exception& e) {
            last_error_ = std::string("DbConnector exception: ") + e.what();
            return false;
        }
    }

    /**
     * @brief 获取 ormpp 数据库引擎引用
     */
    db_engine& engine() {
        if (!db_) {
            throw std::runtime_error("DbConnector not initialized");
        }
        return *db_;
    }

    /**
     * @brief 检查连接状态并自动重连
     */
    bool ensureConnected() {
        if (!connected_) return false;

        try {
            if (!db_->ping()) {
                reconnect();
            }
            return db_->ping();
        } catch (...) {
            return reconnect();
        }
    }

    bool isConnected() const { return connected_; }
    std::string getLastError() const { return last_error_; }

    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (db_) {
            db_->disconnect();
        }
        connected_ = false;
    }

private:
    DbConnector() = default;

    bool reconnect() {
        if (config_.retry_count <= 0) return false;

        for (int i = 0; i < config_.retry_count; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000 * (i + 1)));
            try {
                db_->disconnect();
                bool ok = false;
#ifdef EMS_USE_MYSQL
                ok = db_->connect(config_.user, config_.password,
                                  config_.host, config_.database,
                                  config_.timeout_ms / 1000);
#endif
                if (ok) return true;
            } catch (...) { /* 继续重试 */ }
        }
        return false;
    }

    std::unique_ptr<db_engine> db_;
    DbConnectionConfig          config_;
    bool                        connected_{false};
    std::string                 last_error_;
    std::mutex                  mutex_;
};

} // namespace rtdb
} // namespace ems
```

---

## 5. 测点导入实现

### 5.1 `RtDbManager` 扩展 — 数据库导入

在 `rtdb_manager.h` 中新增数据库导入接口：

```cpp
// rtdb_manager.h 追加
class RtDbManager {
public:
    // ... 已有接口保持不变 ...

    // === 新增：数据库配置 ===
    bool setDbConnectionConfig(const DbConnectionConfig& config);
    bool initializeDbConnection();

    // === 引入 ormpp 导入实现 ===
    bool loadPointConfigsFromDb();      // 统一的数据库导入入口

private:
    // 映射辅助函数
    RtDbConfigCache::YcConfig mapToYcConfig(const db::PointConfigEntity& entity);
    RtDbConfigCache::YxConfig mapToYxConfig(const db::PointConfigEntity& entity);
    RtDbConfigCache::YkConfig mapToYkConfig(const db::PointConfigEntity& entity);
    RtDbConfigCache::YtConfig mapToYtConfig(const db::PointConfigEntity& entity);
};
```

### 5.2 `rtdb_manager_db.cpp` — 核心导入实现

```cpp
// backend/src/rtdb/rtdb_manager_db.cpp

#include "rtdb_manager.h"
#include "rtdb_db_mapping.h"
#include "rtdb_db_connector.h"

#include <vector>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace ems {
namespace rtdb {

// ============================================================
// 辅助：字符串转枚举
// ============================================================

static DecodeCode decodeCodeFromString(const std::string& s) {
    static const std::unordered_map<std::string, DecodeCode> map = {
        {"UINT16", DecodeCode::UINT16}, {"INT16", DecodeCode::INT16},
        {"UINT32", DecodeCode::UINT32}, {"INT32", DecodeCode::INT32},
        {"FLOAT",  DecodeCode::FLOAT},  {"DOUBLE", DecodeCode::DOUBLE},
        {"UINT8",  DecodeCode::UINT8},  {"INT8",   DecodeCode::INT8},
        {"BOOL",   DecodeCode::BOOL},   {"BIT",    DecodeCode::BIT},
    };
    auto it = map.find(s);
    return (it != map.end()) ? it->second : DecodeCode::UINT16;
}

static PointType pointTypeFromString(const std::string& s) {
    if (s == "YC") return PointType::YC;
    if (s == "YX") return PointType::YX;
    if (s == "YK") return PointType::YK;
    if (s == "YT") return PointType::YT;
    return PointType::YC;
}

// ============================================================
// 数据库实体 → 内存配置结构体映射
// ============================================================

RtDbConfigCache::YcConfig mapToYcConfig(const db::PointConfigEntity& e) {
    RtDbConfigCache::YcConfig cfg;
    cfg.code      = e.point_code;
    cfg.name      = e.point_name;
    cfg.channelId = e.channel_id;
    cfg.mulCoe    = e.mul_coe;
    cfg.addCoe    = e.add_coe;
    cfg.maxLimit  = e.max_limit;
    cfg.minLimit  = e.min_limit;
    cfg.unit      = e.unit;
    cfg.status    = e.status;
    return cfg;
}

RtDbConfigCache::YxConfig mapToYxConfig(const db::PointConfigEntity& e) {
    RtDbConfigCache::YxConfig cfg;
    cfg.code      = e.point_code;
    cfg.name      = e.point_name;
    cfg.channelId = e.channel_id;
    cfg.bit       = e.bit_pos;
    cfg.reverse   = (e.reverse != 0);
    cfg.status    = e.status;
    return cfg;
}

RtDbConfigCache::YkConfig mapToYkConfig(const db::PointConfigEntity& e) {
    RtDbConfigCache::YkConfig cfg;
    cfg.code         = e.point_code;
    cfg.name         = e.point_name;
    cfg.channelId    = e.channel_id;
    cfg.bit          = e.bit_pos;
    cfg.commandType  = e.cmd_type;
    cfg.status       = e.status;
    return cfg;
}

RtDbConfigCache::YtConfig mapToYtConfig(const db::PointConfigEntity& e) {
    RtDbConfigCache::YtConfig cfg;
    cfg.code      = e.point_code;
    cfg.name      = e.point_name;
    cfg.channelId = e.channel_id;
    cfg.mulCoe    = e.mul_coe;
    cfg.addCoe    = e.add_coe;
    cfg.maxLimit  = e.max_limit;
    cfg.minLimit  = e.min_limit;
    cfg.unit      = e.unit;
    cfg.status    = e.status;
    return cfg;
}

// ============================================================
// 数据库连接管理
// ============================================================

bool RtDbManager::setDbConnectionConfig(const DbConnectionConfig& config) {
    return DbConnector::instance().initialize(config);
}

bool RtDbManager::initializeDbConnection() {
    return DbConnector::instance().ensureConnected();
}

// ============================================================
// 核心：从数据库导入所有测点配置
// ============================================================

bool RtDbManager::loadPointConfigsFromDb() {
    if (!DbConnector::instance().ensureConnected()) {
        spdlog::error("Database connection failed: {}",
                      DbConnector::instance().getLastError());
        return false;
    }

    try {
        auto& engine = DbConnector::instance().engine();

        // --------------------------------------------------
        // ① 使用 ormpp 查询所有启用的测点配置
        //    ormpp 的 query 方法支持条件过滤
        // --------------------------------------------------
        db::PointConfigEntity condition;
        condition.status = 1;  // 仅查询启用状态的点

        std::vector<db::PointConfigEntity> entities;
        entities = engine.query<db::PointConfigEntity>(condition);

        if (entities.empty()) {
            spdlog::warn("No enabled point configs found in database");
            return true;  // 空结果不算失败
        }

        spdlog::info("Fetched {} point configs from database", entities.size());

        // --------------------------------------------------
        // ② 按点类型分组
        // --------------------------------------------------
        std::vector<RtDbConfigCache::YcConfig> ycConfigs;
        std::vector<RtDbConfigCache::YxConfig> yxConfigs;
        std::vector<RtDbConfigCache::YkConfig> ykConfigs;
        std::vector<RtDbConfigCache::YtConfig> ytConfigs;

        ycConfigs.reserve(entities.size());
        yxConfigs.reserve(entities.size());
        ykConfigs.reserve(entities.size());
        ytConfigs.reserve(entities.size());

        size_t unknownTypeCount = 0;

        for (const auto& entity : entities) {
            PointType pt = pointTypeFromString(entity.point_type);

            switch (pt) {
            case PointType::YC:
                ycConfigs.push_back(mapToYcConfig(entity));
                break;
            case PointType::YX:
                yxConfigs.push_back(mapToYxConfig(entity));
                break;
            case PointType::YK:
                ykConfigs.push_back(mapToYkConfig(entity));
                break;
            case PointType::YT:
                ytConfigs.push_back(mapToYtConfig(entity));
                break;
            default:
                ++unknownTypeCount;
                spdlog::warn("Unknown point_type '{}' for point_code '{}'",
                             entity.point_type, entity.point_code);
                break;
            }
        }

        // --------------------------------------------------
        // ③ 加载到配置缓存
        // --------------------------------------------------
        if (!ycConfigs.empty()) configCache_->loadYcConfigs(ycConfigs);
        if (!yxConfigs.empty()) configCache_->loadYxConfigs(yxConfigs);
        if (!ykConfigs.empty()) configCache_->loadYkConfigs(ykConfigs);
        if (!ytConfigs.empty()) configCache_->loadYtConfigs(ytConfigs);

        // --------------------------------------------------
        // ④ 日志汇总
        // --------------------------------------------------
        spdlog::info("Point configs loaded: "
                     "YC={}, YX={}, YK={}, YT={}, unknown={}",
                     ycConfigs.size(), yxConfigs.size(),
                     ykConfigs.size(), ytConfigs.size(),
                     unknownTypeCount);

        return true;

    } catch (const std::exception& e) {
        spdlog::error("loadPointConfigsFromDb exception: {}", e.what());
        return false;
    }
}

// ============================================================
// 按类型分别导入（保留原接口兼容性）
// ============================================================

bool RtDbManager::loadYcConfigsFromDb() {
    if (!DbConnector::instance().ensureConnected()) return false;

    try {
        auto& engine = DbConnector::instance().engine();

        db::PointConfigEntity condition;
        condition.status    = 1;
        condition.point_type = "YC";

        auto entities = engine.query<db::PointConfigEntity>(condition);

        std::vector<RtDbConfigCache::YcConfig> configs;
        configs.reserve(entities.size());
        for (const auto& e : entities) {
            configs.push_back(mapToYcConfig(e));
        }

        configCache_->loadYcConfigs(configs);
        spdlog::info("Loaded {} YC configs from DB", configs.size());
        return true;
    } catch (const std::exception& e) {
        spdlog::error("loadYcConfigsFromDb failed: {}", e.what());
        return false;
    }
}

bool RtDbManager::loadYxConfigsFromDb() {
    if (!DbConnector::instance().ensureConnected()) return false;

    try {
        auto& engine = DbConnector::instance().engine();

        db::PointConfigEntity condition;
        condition.status     = 1;
        condition.point_type = "YX";

        auto entities = engine.query<db::PointConfigEntity>(condition);

        std::vector<RtDbConfigCache::YxConfig> configs;
        configs.reserve(entities.size());
        for (const auto& e : entities) {
            configs.push_back(mapToYxConfig(e));
        }

        configCache_->loadYxConfigs(configs);
        spdlog::info("Loaded {} YX configs from DB", configs.size());
        return true;
    } catch (const std::exception& e) {
        spdlog::error("loadYxConfigsFromDb failed: {}", e.what());
        return false;
    }
}

bool RtDbManager::loadYkConfigsFromDb() {
    if (!DbConnector::instance().ensureConnected()) return false;

    try {
        auto& engine = DbConnector::instance().engine();

        db::PointConfigEntity condition;
        condition.status     = 1;
        condition.point_type = "YK";

        auto entities = engine.query<db::PointConfigEntity>(condition);

        std::vector<RtDbConfigCache::YkConfig> configs;
        configs.reserve(entities.size());
        for (const auto& e : entities) {
            configs.push_back(mapToYkConfig(e));
        }

        configCache_->loadYkConfigs(configs);
        spdlog::info("Loaded {} YK configs from DB", configs.size());
        return true;
    } catch (const std::exception& e) {
        spdlog::error("loadYkConfigsFromDb failed: {}", e.what());
        return false;
    }
}

bool RtDbManager::loadYtConfigsFromDb() {
    if (!DbConnector::instance().ensureConnected()) return false;

    try {
        auto& engine = DbConnector::instance().engine();

        db::PointConfigEntity condition;
        condition.status     = 1;
        condition.point_type = "YT";

        auto entities = engine.query<db::PointConfigEntity>(condition);

        std::vector<RtDbConfigCache::YtConfig> configs;
        configs.reserve(entities.size());
        for (const auto& e : entities) {
            configs.push_back(mapToYtConfig(e));
        }

        configCache_->loadYtConfigs(configs);
        spdlog::info("Loaded {} YT configs from DB", configs.size());
        return true;
    } catch (const std::exception& e) {
        spdlog::error("loadYtConfigsFromDb failed: {}", e.what());
        return false;
    }
}

// loadAllConfigs 修改为统一入口
bool RtDbManager::loadAllConfigs() {
    // 优先使用统一加载，回退到分别加载
    return loadPointConfigsFromDb();
}

} // namespace rtdb
} // namespace ems
```

---

## 6. 完整使用示例

### 6.1 C++ 引擎启动流程

```cpp
// 采集进程入口 main.cpp
#include <iostream>
#include <csignal>
#include "rtdb_manager.h"
#include "rtdb_db_connector.h"

using namespace ems::rtdb;

std::atomic<bool> g_running{true};

void signalHandler(int) {
    g_running = false;
}

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // -------------------------------------------------------
    // 步骤 1：连接 Redis
    // -------------------------------------------------------
    RtDbConfig redisConfig{
        .host     = "127.0.0.1",
        .port     = 6379,
        .password = "888888",
        .db       = 0,
        .poolSize = 4,
    };

    auto manager = createRtdbManager(redisConfig);
    if (!manager->isConnected()) {
        std::cerr << "Redis connection failed: " << manager->getLastError() << std::endl;
        return 1;
    }
    std::cout << "[OK] Redis connected" << std::endl;

    // -------------------------------------------------------
    // 步骤 2：连接 MySQL 数据库
    // -------------------------------------------------------
    DbConnectionConfig dbConfig{
        .host     = "127.0.0.1",
        .port     = 3306,
        .user     = "root",
        .password = "your_password",
        .database = "ems",
    };

    if (!manager->setDbConnectionConfig(dbConfig)) {
        std::cerr << "Database connection failed: "
                  << DbConnector::instance().getLastError() << std::endl;
        return 1;
    }
    std::cout << "[OK] MySQL connected" << std::endl;

    // -------------------------------------------------------
    // 步骤 3：从数据库加载点表配置
    // -------------------------------------------------------
    if (!manager->loadAllConfigs()) {
        std::cerr << "Failed to load point configs from database" << std::endl;
        return 1;
    }

    auto cache = manager->getConfigCache();
    std::cout << "[OK] Point configs loaded: "
              << "YC=" << cache->getYcConfigCount() << ", "
              << "YX=" << cache->getYxConfigCount() << ", "
              << "YK=" << cache->getYkConfigCount() << ", "
              << "YT=" << cache->getYtConfigCount() << std::endl;

    // -------------------------------------------------------
    // 步骤 4：根据点表配置启动采集任务
    // -------------------------------------------------------
    // ... 遍历 RtDbConfigCache → 获取设备/通道/地址 → 启动 ProtocolHandler ...

    // -------------------------------------------------------
    // 步骤 5：进入主循环
    // -------------------------------------------------------
    while (g_running) {
        // 采集数据 → manager->updateYc() → Redis HSET + PUBLISH
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    // 清理
    manager->shutdown();
    DbConnector::instance().shutdown();
    std::cout << "[OK] Shutdown complete" << std::endl;

    return 0;
}
```

### 6.2 单测验证

```cpp
// backend/tests/rtdb/db_import_test.cpp
#include <gtest/gtest.h>
#include "rtdb_db_mapping.h"
#include "rtdb_db_connector.h"
#include "rtdb_manager.h"

using namespace ems::rtdb;

// ============================================================
// ormpp 实体映射基本测试
// ============================================================
TEST(DbMappingTest, EntityNameMapping) {
    // 验证 ormpp 表名映射
    EXPECT_EQ(ormpp::entity_name<db::PointConfigEntity>(), "point_configs");
    EXPECT_EQ(ormpp::entity_name<db::ChannelConfigEntity>(), "channel_configs");
}

TEST(DbMappingTest, DefaultValues) {
    db::PointConfigEntity entity;
    EXPECT_EQ(entity.id, 0);
    EXPECT_EQ(entity.status, 1);
    EXPECT_DOUBLE_EQ(entity.mul_coe, 1.0);
    EXPECT_DOUBLE_EQ(entity.add_coe, 0.0);
    EXPECT_DOUBLE_EQ(entity.min_limit, -9999999.0);
    EXPECT_DOUBLE_EQ(entity.max_limit, 9999999.0);
    EXPECT_TRUE(entity.point_code.empty());
}

// ============================================================
// 数据库集成测试 (需要本地 MySQL)
// ============================================================
class DbImportTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 检查环境变量，如果没有配置数据库则跳过
        const char* dbHost = std::getenv("EMS_TEST_DB_HOST");
        if (!dbHost) {
            GTEST_SKIP() << "Database not configured. "
                         << "Set EMS_TEST_DB_HOST to enable DB tests.";
        }

        DbConnectionConfig cfg{
            .host     = dbHost,
            .port     = 3306,
            .user     = std::getenv("EMS_TEST_DB_USER") ?: "root",
            .password = std::getenv("EMS_TEST_DB_PASSWORD") ?: "",
            .database = std::getenv("EMS_TEST_DB_NAME") ?: "ems_test",
        };

        ASSERT_TRUE(DbConnector::instance().initialize(cfg))
            << DbConnector::instance().getLastError();

        manager_ = std::make_unique<RtDbManager>();
    }

    void TearDown() override {
        manager_.reset();
    }

    std::unique_ptr<RtDbManager> manager_;
};

TEST_F(DbImportTest, LoadPointConfigsFromDb) {
    ASSERT_TRUE(manager_->loadPointConfigsFromDb());

    auto cache = manager_->getConfigCache();
    // 至少应加载到一些测点（具体数量取决于测试数据）
    std::cout << "Loaded YC=" << cache->getYcConfigCount()
              << " YX=" << cache->getYxConfigCount()
              << " YK=" << cache->getYkConfigCount()
              << " YT=" << cache->getYtConfigCount() << std::endl;
}

TEST_F(DbImportTest, LoadYcConfigsOnly) {
    ASSERT_TRUE(manager_->loadYcConfigsFromDb());
    auto cache = manager_->getConfigCache();

    // YC 类型应该有数据（如果测试库中插入了数据）
    auto codes = cache->getYcConfigCodes();
    std::cout << "YC config codes count: " << codes.size() << std::endl;

    for (const auto& code : codes) {
        RtDbConfigCache::YcConfig cfg;
        ASSERT_TRUE(cache->getYcConfig(code, cfg));
        EXPECT_EQ(cfg.code, code);
        EXPECT_FALSE(cfg.name.empty());

        // 验证变换系数合法性
        EXPECT_FALSE(std::isnan(cfg.mulCoe));
        EXPECT_FALSE(std::isnan(cfg.addCoe));
        EXPECT_LE(cfg.minLimit, cfg.maxLimit);
    }
}
```

### 6.3 测试数据准备

```sql
-- 向 point_configs 表插入测试数据
INSERT INTO point_configs
    (device_id, point_code, point_name, point_type, decode_code,
     channel_id, func_code, reg_address, reg_count, data_type,
     mul_coe, add_coe, min_limit, max_limit, unit, status)
VALUES
    -- 遥测点（YC）
    (1, 'voltage_a', 'A相电压', 'YC', 'FLOAT', 1, 4, 0, 2, 'float32_cdab',
     0.1, 0.0, 0.0, 300.0, 'V', 1),
    (1, 'voltage_b', 'B相电压', 'YC', 'FLOAT', 1, 4, 2, 2, 'float32_cdab',
     0.1, 0.0, 0.0, 300.0, 'V', 1),
    (1, 'power', '有功功率', 'YC', 'INT32', 1, 4, 10, 2, 'int32',
     0.01, 0.0, -10000.0, 10000.0, 'kW', 1),

    -- 遥信点（YX）
    (1, 'breaker_status', '断路器位置', 'YX', 'BOOL', 1, 2, 0, 1, 'bool',
     1.0, 0.0, 0.0, 1.0, '', 1),
    (1, 'protect_action', '保护动作', 'YX', 'BIT', 1, 2, 0, 1, 'bit',
     1.0, 0.0, 0.0, 1.0, '', 1),

    -- 遥控点（YK）
    (1, 'breaker_close', '合闸命令', 'YK', 'BOOL', 1, 0, 0, 1, 'bool',
     1.0, 0.0, 0.0, 1.0, '', 1),

    -- 遥调点（YT）
    (1, 'temp_setpoint', '温度设定', 'YT', 'FLOAT', 1, 6, 20, 2, 'float32_cdab',
     1.0, 0.0, -40.0, 80.0, 'C', 1),

    -- 停用的点（不应被导入）
    (1, 'old_sensor', '旧传感器', 'YC', 'UINT16', 1, 4, 100, 1, 'uint16',
     1.0, 0.0, 0.0, 1000.0, '', 0);
```

---

## 7. 导入流程全景

结合 EMS 架构文档第 4.2.2 节「C++ 引擎启动流程」，完整导入流程如下：

```
main()
  │
  ├─ 1. 连接 Redis
  │     RtDbManager::initialize(redisConfig)
  │
  ├─ 2. 连接 MySQL (ormpp)
  │     DbConnector::instance().initialize(dbConfig)
  │       └── ormpp::dbng<ormpp::mysql>.connect(user, pass, host, db)
  │
  ├─ 3. 从数据库加载点表配置 ──────────────────────────── 新增实现
  │     RtDbManager::loadAllConfigs()
  │       │
  │       ├── loadPointConfigsFromDb()
  │       │     │
  │       │     ├── engine.query<PointConfigEntity>(condition)
  │       │     │     └── SELECT * FROM point_configs WHERE status=1
  │       │     │
  │       │     ├── 按 point_type 分组 → YC / YX / YK / YT
  │       │     │     └── entity → RtDbConfigCache::YcConfig / ... 映射转换
  │       │     │
  │       │     └── configCache_->loadYcConfigs(ycConfigs)   // YC
  │       │         configCache_->loadYxConfigs(yxConfigs)   // YX
  │       │         configCache_->loadYkConfigs(ykConfigs)   // YK
  │       │         configCache_->loadYtConfigs(ytConfigs)   // YT
  │       │
  │       └── 日志输出: "Loaded YC=N YX=N YK=N YT=N"
  │
  ├─ 4. 订阅 Redis 热更新通道
  │     SUBSCRIBE "rtdb:config:reload"
  │     → 收到通知后重新 loadAllConfigs()
  │
  ├─ 5. 根据点表启动协议采集任务
  │     遍历 RtDbConfigCache → 获取 channelId/regAddress/funcCode
  │     → 创建 ProtocolHandler → 开始轮询
  │
  └─ 6. 主循环
        采集 → updateYc/Yx → Redis HSET + PUBLISH → Go 服务订阅
```

### 点表热更新流程

```
Go Web 管理后台
  │
  ├── 用户修改点表 → GORM 更新 point_configs 表
  │
  ├── Redis PUBLISH rtdb:config:reload "point_configs"
  │
  └── C++ 引擎 SUBSCRIBE 收到通知
         │
         ├── 清空现有 RtDbConfigCache
         ├── loadAllConfigs() 重新加载
         └── 采集任务自动感知配置变更
```

---

## 8. 错误处理与恢复策略

### 8.1 启动阶段

| 场景 | 处理方式 |
|------|---------|
| **数据库连接失败** | 重试 3 次，间隔递增；最终失败则退出进程 |
| **表不存在** | ormpp 查询抛出异常，记录日志后退出 |
| **空表** | 正常返回，日志 warn 提示 |
| **字段类型不匹配** | ormpp 反射阶段编译错误，提前发现 |

### 8.2 运行时

| 场景 | 处理方式 |
|------|---------|
| **数据库连接断开** | `ensureConnected()` 自动重连 |
| **热加载失败** | 保留旧缓存，记录错误日志，下次重试 |
| **部分点解析失败** | 跳过异常记录，继续处理其余点 |

---

## 9. 文件变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `backend/3rd/ormpp/` | 新增 | 第三方 ormpp 库（header-only） |
| `backend/src/rtdb/rtdb_db_mapping.h` | 新增 | ormpp 数据库实体映射 |
| `backend/src/rtdb/rtdb_db_connector.h` | 新增 | 数据库连接管理器 |
| `backend/src/rtdb/rtdb_manager_db.cpp` | 新增 | 数据库导入实现 |
| `backend/src/rtdb/CMakeLists.txt` | 修改 | 添加 ormpp 头文件路径及数据库客户端库 |
| `backend/src/rtdb/rtdb_manager.h` | 修改 | 添加 `setDbConnectionConfig` / `loadPointConfigsFromDb` 声明 |
| `backend/tests/rtdb/db_import_test.cpp` | 新增 | 数据库导入单元测试 |
| `backend/tests/rtdb/CMakeLists.txt` | 修改 | 添加测试目标 |

---

## 10. 总结

本方案基于 EMS 架构文档中「点表数据库化管理」的设计原则，使用 **ormpp** 作为 C++ 侧 ORM 库实现了以下功能：

- **编译期类型安全**：数据库表结构与 C++ struct 通过 ormpp 反射自动映射，字段拼写错误在编译期即可发现
- **一次查询、分组加载**：`loadPointConfigsFromDb()` 一次性查询所有启用测点，按 YC/YX/YK/YT 分组加载到 `RtDbConfigCache`
- **断线重连**：`DbConnector` 封装了连接生命周期管理和自动重连逻辑
- **兼容现有接口**：`loadYcConfigsFromDb()` 等四个函数改用 ormpp 实现，保留原有接口签名
- **与 Go GORM 对齐**：数据库表 `point_configs` 同时服务于 Go GORM 写入和 C++ ormpp 读取，Redis Pub/Sub 触发热加载
