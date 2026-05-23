/**
 * @file db_manager.cpp
 * @brief 数据库连接管理实现
 *
 * 使用 ormpp 库操作 MySQL/SQLite。
 * ormpp 需要 MySQL/SQLite 客户端库头文件才能编译对应后端。
 * 当缺少对应开发包时，该后端功能自动禁用。
 */
#include "db/db_manager.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <array>          // for YLT_REFL macro (std::array)
#include <nlohmann/json.hpp>
#include "util/conf.hpp"

// ============================================================
// 引入 ormpp 反射宏所需的头文件 (纯 C++ 标准库，无系统依赖)
// ============================================================
#include <iguana/ylt/reflection/user_reflect_macro.hpp>

// ============================================================
// YLT_REFL 注册所有实体类 (此处需要 ormpp 反射基础设施)
// 这些宏在 struct 所在的命名空间上下文中展开
// ============================================================
namespace ems::db {
YLT_REFL(DeviceEntity,
    id, code, name, device_type, group_id, description, enable, created_at, updated_at);

YLT_REFL(ChannelEntity,
    id, code, name, device_id, protocol_type, conn_type,
    ip, port, com_port, baud_rate, data_bits, stop_bits, parity,
    rtu_addr, timeout, enable, created_at, updated_at);

YLT_REFL(PointYcEntity,
    id, code, name, channel_id, device_id,
    protocol_type, slave_id, reg_addr, bit_position,
    data_id, casdu, ioa, object_ref,
    decode_code, mul_coe, add_coe, min_limit, max_limit, unit,
    enable, description, created_at, updated_at);

YLT_REFL(PointYxEntity,
    id, code, name, channel_id, device_id,
    protocol_type, slave_id, reg_addr, bit_position,
    data_id, casdu, ioa, object_ref,
    decode_code, bit, reverse_flag,
    enable, description, created_at, updated_at);

YLT_REFL(PointYkEntity,
    id, code, name, channel_id, device_id,
    protocol_type, slave_id, reg_addr, bit_position,
    data_id, casdu, ioa, object_ref,
    decode_code, bit, command_type, related_yx_id,
    enable, description, created_at, updated_at);

YLT_REFL(PointYtEntity,
    id, code, name, channel_id, device_id,
    protocol_type, slave_id, reg_addr, bit_position,
    data_id, casdu, ioa, object_ref,
    decode_code, mul_coe, add_coe, min_limit, max_limit, unit,
    enable, description, created_at, updated_at);

YLT_REFL(PointCollectEntity,
    id, point_code, point_type, channel_id, device_id,
    raw_value, eng_value, quality, collect_time);
} // namespace ems::db

// ============================================================
// 条件编译 MySQL/SQLite 后端
// 仅当对应开发包可用时，才包含 ormpp 的对应实现头文件
// ============================================================
#if __has_include(<mysql/mysql.h>)
#include <ormpp/mysql.hpp>
#define EMS_DB_HAS_MYSQL
#endif

#if __has_include(<sqlite3.h>)
#include <ormpp/sqlite.hpp>
#define EMS_DB_HAS_SQLITE
#endif

namespace ems {
namespace db {

// ============================================================
// 内部实现类 (PIMPL)
// ============================================================
class DbManager::Impl {
public:
    ~Impl() { disconnect(); }

    // ---- MySQL 连接 ----
    bool connect(const DbConfig& cfg) {
#ifdef EMS_DB_HAS_MYSQL
        using_mysql_ = true;

        if (auto tp = std::make_tuple(cfg.host, cfg.user, cfg.password,
                                       cfg.dbname, std::optional<int>(cfg.timeout),
                                       std::optional<int>(cfg.port));
            !mysql_.connect(tp)) {
            last_err_ = mysql_.get_last_error();
            return false;
        }
        return true;
#else
        last_err_ = "MySQL support not compiled (install libmysqlclient-dev)";
        return false;
#endif
    }

    // ---- SQLite 连接 ----
    bool connect_sqlite(const std::string& db_path) {
#ifdef EMS_DB_HAS_SQLITE
        using_mysql_ = false;

        if (auto tp = std::make_tuple(std::string(""), std::string(""),
                                       std::string(""), db_path,
                                       std::optional<int>{}, std::optional<int>{});
            !sqlite_.connect(tp)) {
            last_err_ = sqlite_.get_last_error();
            return false;
        }
        return true;
#else
        last_err_ = "SQLite support not compiled (install libsqlite3-dev)";
        return false;
#endif
    }

    void disconnect() {
#ifdef EMS_DB_HAS_MYSQL
        if (using_mysql_) {
            mysql_.disconnect();
            return;
        }
#endif
#ifdef EMS_DB_HAS_SQLITE
        sqlite_.disconnect();
#endif
    }

    bool is_connected() const {
        return true; // ormpp 无 is_connected，由业务层维护
    }

    std::string last_error() const { return last_err_; }

    // ---- 设备 CRUD ----

    std::vector<DeviceEntity> query_all_devices() {
#ifdef EMS_DB_HAS_MYSQL
        return exec_query<DeviceEntity>("");
#else
        return {};
#endif
    }

    DeviceEntity query_device_by_id(int64_t id) {
#ifdef EMS_DB_HAS_MYSQL
        auto v = exec_query<DeviceEntity>("id=" + std::to_string(id));
        return v.empty() ? DeviceEntity{} : v[0];
#else
        return {};
#endif
    }

    DeviceEntity query_device_by_code(const std::string& code) {
#ifdef EMS_DB_HAS_MYSQL
        auto v = exec_query<DeviceEntity>("code='" + esc(code) + "'");
        return v.empty() ? DeviceEntity{} : v[0];
#else
        return {};
#endif
    }

    bool insert_device(const DeviceEntity& device) {
#ifdef EMS_DB_HAS_MYSQL
        return exec_write([this, &device]() { return mysql_.insert(device); });
#else
        return false;
#endif
    }

    bool update_device(const DeviceEntity& device) {
#ifdef EMS_DB_HAS_MYSQL
        return exec_write([this, &device]() { return mysql_.update(device); });
#else
        return false;
#endif
    }

    bool delete_device(int64_t id) {
#ifdef EMS_DB_HAS_MYSQL
        return exec_write([this, id]() {
            return mysql_.delete_records_s<DeviceEntity>("id=" + std::to_string(id));
        });
#else
        return false;
#endif
    }

    // ---- 通道 CRUD ----

    std::vector<ChannelEntity> query_all_channels() {
#ifdef EMS_DB_HAS_MYSQL
        return exec_query<ChannelEntity>("");
#else
        return {};
#endif
    }

    std::vector<ChannelEntity> query_channels_by_device(int64_t device_id) {
#ifdef EMS_DB_HAS_MYSQL
        return exec_query<ChannelEntity>("device_id=" + std::to_string(device_id));
#else
        return {};
#endif
    }

    ChannelEntity query_channel_by_id(int64_t id) {
#ifdef EMS_DB_HAS_MYSQL
        auto v = exec_query<ChannelEntity>("id=" + std::to_string(id));
        return v.empty() ? ChannelEntity{} : v[0];
#else
        return {};
#endif
    }

    ChannelEntity query_channel_by_code(const std::string& code) {
#ifdef EMS_DB_HAS_MYSQL
        auto v = exec_query<ChannelEntity>("code='" + esc(code) + "'");
        return v.empty() ? ChannelEntity{} : v[0];
#else
        return {};
#endif
    }

    bool insert_channel(const ChannelEntity& ch) {
#ifdef EMS_DB_HAS_MYSQL
        return exec_write([this, &ch]() { return mysql_.insert(ch); });
#else
        return false;
#endif
    }

    bool update_channel(const ChannelEntity& ch) {
#ifdef EMS_DB_HAS_MYSQL
        return exec_write([this, &ch]() { return mysql_.update(ch); });
#else
        return false;
#endif
    }

    bool delete_channel(int64_t id) {
#ifdef EMS_DB_HAS_MYSQL
        return exec_write([this, id]() {
            return mysql_.delete_records_s<ChannelEntity>("id=" + std::to_string(id));
        });
#else
        return false;
#endif
    }

    // ---- 测点配置查询 ----

    std::vector<PointYcEntity> query_yc_points(int64_t channel_id) {
#ifdef EMS_DB_HAS_MYSQL
        if (channel_id > 0)
            return exec_query<PointYcEntity>("channel_id=" + std::to_string(channel_id));
        return exec_query<PointYcEntity>("");
#else
        return {};
#endif
    }

    std::vector<PointYxEntity> query_yx_points(int64_t channel_id) {
#ifdef EMS_DB_HAS_MYSQL
        if (channel_id > 0)
            return exec_query<PointYxEntity>("channel_id=" + std::to_string(channel_id));
        return exec_query<PointYxEntity>("");
#else
        return {};
#endif
    }

    std::vector<PointYkEntity> query_yk_points(int64_t channel_id) {
#ifdef EMS_DB_HAS_MYSQL
        if (channel_id > 0)
            return exec_query<PointYkEntity>("channel_id=" + std::to_string(channel_id));
        return exec_query<PointYkEntity>("");
#else
        return {};
#endif
    }

    std::vector<PointYtEntity> query_yt_points(int64_t channel_id) {
#ifdef EMS_DB_HAS_MYSQL
        if (channel_id > 0)
            return exec_query<PointYtEntity>("channel_id=" + std::to_string(channel_id));
        return exec_query<PointYtEntity>("");
#else
        return {};
#endif
    }

    // ---- 采集数据导入 ----

    int batch_import_collect_data(const std::vector<CollectImportData>& data_list) {
#ifdef EMS_DB_HAS_MYSQL
        if (data_list.empty()) return 0;

        std::vector<PointCollectEntity> entities;
        entities.reserve(data_list.size());
        for (const auto& d : data_list) {
            PointCollectEntity e;
            e.point_code   = d.point_code;
            e.point_type   = d.point_type;
            e.channel_id   = d.channel_id;
            e.device_id    = d.device_id;
            e.raw_value    = d.raw_value;
            e.eng_value    = d.eng_value;
            e.quality      = d.quality;
            e.collect_time = d.collect_time;
            entities.push_back(std::move(e));
        }

        bool ok = exec_write([this, &entities]() { return mysql_.insert(entities); });
        return ok ? static_cast<int>(entities.size()) : -1;
#else
        return -1;
#endif
    }

private:
#ifdef EMS_DB_HAS_MYSQL
    template<typename T>
    std::vector<T> exec_query(const std::string& where) {
        auto result = where.empty()
            ? mysql_.query_s<T>()
            : mysql_.query_s<T>(where);
        if (mysql_.has_error()) {
            last_err_ = mysql_.get_last_error();
        }
        return result;
    }

    template<typename F>
    bool exec_write(F&& f) {
        bool ok = f();
        if (!ok && mysql_.has_error()) {
            last_err_ = mysql_.get_last_error();
        }
        return ok;
    }

    // SQL 注入简单转义
    static std::string esc(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            if (c == '\'') out += "\\'";
            else if (c == '\\') out += "\\\\";
            else out += c;
        }
        return out;
    }

    bool using_mysql_ = true;
    ormpp::dbng<ormpp::mysql> mysql_;
#else
    template<typename T>
    std::vector<T> noop_query(const std::string&) { return {}; }
    bool noop_write() { return false; }
#endif

#ifdef EMS_DB_HAS_SQLITE
    ormpp::dbng<ormpp::sqlite> sqlite_;
#endif

    std::string last_err_;
};

// ============================================================
// DbManager 公有接口 (委托给 Impl)
// ============================================================

DbManager::DbManager() : impl_(std::make_unique<Impl>()) {}
DbManager::~DbManager() = default;
DbManager::DbManager(DbManager&&) noexcept = default;
DbManager& DbManager::operator=(DbManager&&) noexcept = default;

bool DbManager::connect(const DbConfig& config) {
    return impl_->connect(config);
}

bool DbManager::connect_sqlite(const std::string& db_path) {
    return impl_->connect_sqlite(db_path);
}

void DbManager::disconnect() {
    impl_->disconnect();
}

bool DbManager::is_connected() const {
    return impl_->is_connected();
}

std::string DbManager::last_error() const {
    return impl_->last_error();
}

// ---- 设备 ----

std::vector<DeviceEntity> DbManager::query_all_devices() {
    return impl_->query_all_devices();
}

DeviceEntity DbManager::query_device_by_id(int64_t id) {
    return impl_->query_device_by_id(id);
}

DeviceEntity DbManager::query_device_by_code(const std::string& code) {
    return impl_->query_device_by_code(code);
}

bool DbManager::insert_device(const DeviceEntity& device) {
    return impl_->insert_device(device);
}

bool DbManager::update_device(const DeviceEntity& device) {
    return impl_->update_device(device);
}

bool DbManager::delete_device(int64_t id) {
    return impl_->delete_device(id);
}

// ---- 通道 ----

std::vector<ChannelEntity> DbManager::query_all_channels() {
    return impl_->query_all_channels();
}

std::vector<ChannelEntity> DbManager::query_channels_by_device(int64_t device_id) {
    return impl_->query_channels_by_device(device_id);
}

ChannelEntity DbManager::query_channel_by_id(int64_t id) {
    return impl_->query_channel_by_id(id);
}

ChannelEntity DbManager::query_channel_by_code(const std::string& code) {
    return impl_->query_channel_by_code(code);
}

bool DbManager::insert_channel(const ChannelEntity& channel) {
    return impl_->insert_channel(channel);
}

bool DbManager::update_channel(const ChannelEntity& channel) {
    return impl_->update_channel(channel);
}

bool DbManager::delete_channel(int64_t id) {
    return impl_->delete_channel(id);
}

// ---- 测点 ----

std::vector<PointYcEntity> DbManager::query_yc_points(int64_t channel_id) {
    return impl_->query_yc_points(channel_id);
}

std::vector<PointYxEntity> DbManager::query_yx_points(int64_t channel_id) {
    return impl_->query_yx_points(channel_id);
}

std::vector<PointYkEntity> DbManager::query_yk_points(int64_t channel_id) {
    return impl_->query_yk_points(channel_id);
}

std::vector<PointYtEntity> DbManager::query_yt_points(int64_t channel_id) {
    return impl_->query_yt_points(channel_id);
}

// ---- 导入 ----

int DbManager::batch_import_collect_data(const std::vector<CollectImportData>& data_list) {
    return impl_->batch_import_collect_data(data_list);
}

// ============================================================
// 默认配置 (从 JSON 配置文件读取, 环境变量覆盖)
// ============================================================

/**
 * @brief db.json 格式示例:
 *
 * {
 *     "host": "127.0.0.1",
 *     "port": 3306,
 *     "user": "root",
 *     "password": "",
 *     "dbname": "ems",
 *     "timeout": 5,
 *     "conn_num": 4
 * }
 */
DbConfig get_default_db_config() {
    DbConfig cfg;
    std::string conf_file;

    // 1. 优先使用环境变量 EMS_DB_CONF 指定的配置文件路径
    const char* env_cfg = std::getenv("EMS_DB_CONF");
    if (env_cfg && env_cfg[0]) {
        conf_file = env_cfg;
    } else {
        // 2. 默认: /usr/ems/conf/db.json
        conf_file = dbConfPath();
    }

    // 尝试从 JSON 文件读取
    std::ifstream ifs(conf_file);
    if (ifs.is_open()) {
        try {
            using json = nlohmann::json;
            json j;
            ifs >> j;

            if (j.contains("host"))     cfg.host     = j["host"].get<std::string>();
            if (j.contains("port"))     cfg.port     = j["port"].get<int>();
            if (j.contains("user"))     cfg.user     = j["user"].get<std::string>();
            if (j.contains("password")) cfg.password = j["password"].get<std::string>();
            if (j.contains("dbname"))   cfg.dbname   = j["dbname"].get<std::string>();
            if (j.contains("timeout"))  cfg.timeout  = j["timeout"].get<int>();
            if (j.contains("conn_num")) cfg.conn_num = j["conn_num"].get<int>();
        } catch (const std::exception& e) {
            // JSON 解析失败, 回退到默认值 + 环境变量
        }
    }

    // 3. 环境变量覆盖 (优先级最高)
    const char* env_host = std::getenv("EMS_DB_HOST");
    const char* env_port = std::getenv("EMS_DB_PORT");
    const char* env_user = std::getenv("EMS_DB_USER");
    const char* env_pass = std::getenv("EMS_DB_PASS");
    const char* env_name = std::getenv("EMS_DB_NAME");

    if (env_host) cfg.host     = env_host;
    if (env_port) cfg.port     = std::atoi(env_port);
    if (env_user) cfg.user     = env_user;
    if (env_pass) cfg.password = env_pass;
    if (env_name) cfg.dbname   = env_name;

    return cfg;
}

} // namespace db
} // namespace ems
