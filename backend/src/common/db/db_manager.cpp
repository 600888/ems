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
#include <unistd.h>
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
YLT_REFL(device,
    id, code, name, device_type, group_id, description, enable, created_at, updated_at);

YLT_REFL(channel,
    id, code, name, device_id, protocol_type, conn_type,
    ip, port, com_port, baud_rate, data_bits, stop_bits, parity,
    rtu_addr, timeout, enable, created_at, updated_at);

YLT_REFL(point_yc,
    id, code, name, channel_id, device_id,
    protocol_type, func_code, slave_id, reg_addr, bit_position,
    data_id, casdu, ioa, object_ref,
    decode_code, mul_coe, add_coe, min_limit, max_limit, unit,
    enable, description, created_at, updated_at);

YLT_REFL(point_yx,
    id, code, name, channel_id, device_id,
    protocol_type, func_code, slave_id, reg_addr, bit_position,
    data_id, casdu, ioa, object_ref,
    decode_code, bit, reverse_flag,
    enable, description, created_at, updated_at);

YLT_REFL(point_yk,
    id, code, name, channel_id, device_id,
    protocol_type, func_code, slave_id, reg_addr, bit_position,
    data_id, casdu, ioa, object_ref,
    decode_code, bit, command_type, related_yx_id,
    enable, description, created_at, updated_at);

YLT_REFL(point_yt,
    id, code, name, channel_id, device_id,
    protocol_type, func_code, slave_id, reg_addr, bit_position,
    data_id, casdu, ioa, object_ref,
    decode_code, mul_coe, add_coe, min_limit, max_limit, unit,
    enable, description, created_at, updated_at);

YLT_REFL(point_collect,
    id, point_code, point_type, channel_id, device_id,
    raw_value, eng_value, quality, collect_time);
} // namespace ems::db

// ============================================================
// 条件编译 MySQL/SQLite 后端
// MySQL 检测由 CMake 的 EMS_DB_HAS_MYSQL + ORMPP_ENABLE_MYSQL 控制
// ============================================================
#ifdef EMS_DB_HAS_MYSQL
#include <mysql/mysql.h>
#include <ormpp/mysql.hpp>
#include <ormpp/dbng.hpp>
#include <ormpp/entity.hpp>
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

    std::vector<device> query_all_devices() {
#ifdef EMS_DB_HAS_MYSQL
        return exec_query<device>("");
#else
        return {};
#endif
    }

    device query_device_by_id(int64_t id) {
#ifdef EMS_DB_HAS_MYSQL
        auto v = exec_query<device>("id=" + std::to_string(id));
        return v.empty() ? device{} : v[0];
#else
        return {};
#endif
    }

    device query_device_by_code(const std::string& code) {
#ifdef EMS_DB_HAS_MYSQL
        auto v = exec_query<device>("code='" + esc(code) + "'");
        return v.empty() ? device{} : v[0];
#else
        return {};
#endif
    }

    bool insert_device(const device& device) {
#ifdef EMS_DB_HAS_MYSQL
        return exec_write([this, &device]() { return mysql_.insert(device); });
#else
        return false;
#endif
    }

    bool update_device(const device& device) {
#ifdef EMS_DB_HAS_MYSQL
        return exec_write([this, &device]() { return mysql_.update(device); });
#else
        return false;
#endif
    }

    bool delete_device(int64_t id) {
#ifdef EMS_DB_HAS_MYSQL
        return exec_write([this, id]() {
            return mysql_.delete_records_s<device>("id=" + std::to_string(id));
        });
#else
        return false;
#endif
    }

    // ---- 通道 CRUD ----

    std::vector<channel> query_all_channels() {
#ifdef EMS_DB_HAS_MYSQL
        return exec_query<channel>("");
#else
        return {};
#endif
    }

    std::vector<channel> query_channels_by_device(int64_t device_id) {
#ifdef EMS_DB_HAS_MYSQL
        return exec_query<channel>("device_id=" + std::to_string(device_id));
#else
        return {};
#endif
    }

    channel query_channel_by_id(int64_t id) {
#ifdef EMS_DB_HAS_MYSQL
        auto v = exec_query<channel>("id=" + std::to_string(id));
        return v.empty() ? channel{} : v[0];
#else
        return {};
#endif
    }

    channel query_channel_by_code(const std::string& code) {
#ifdef EMS_DB_HAS_MYSQL
        auto v = exec_query<channel>("code='" + esc(code) + "'");
        return v.empty() ? channel{} : v[0];
#else
        return {};
#endif
    }

    bool insert_channel(const channel& ch) {
#ifdef EMS_DB_HAS_MYSQL
        return exec_write([this, &ch]() { return mysql_.insert(ch); });
#else
        return false;
#endif
    }

    bool update_channel(const channel& ch) {
#ifdef EMS_DB_HAS_MYSQL
        return exec_write([this, &ch]() { return mysql_.update(ch); });
#else
        return false;
#endif
    }

    bool delete_channel(int64_t id) {
#ifdef EMS_DB_HAS_MYSQL
        return exec_write([this, id]() {
            return mysql_.delete_records_s<channel>("id=" + std::to_string(id));
        });
#else
        return false;
#endif
    }

    // ---- 测点配置查询 ----

    std::vector<point_yc> query_yc_points(int64_t channel_id) {
#ifdef EMS_DB_HAS_MYSQL
        if (channel_id > 0)
            return exec_query<point_yc>("channel_id=" + std::to_string(channel_id));
        return exec_query<point_yc>("");
#else
        return {};
#endif
    }

    std::vector<point_yx> query_yx_points(int64_t channel_id) {
#ifdef EMS_DB_HAS_MYSQL
        if (channel_id > 0)
            return exec_query<point_yx>("channel_id=" + std::to_string(channel_id));
        return exec_query<point_yx>("");
#else
        return {};
#endif
    }

    std::vector<point_yk> query_yk_points(int64_t channel_id) {
#ifdef EMS_DB_HAS_MYSQL
        if (channel_id > 0)
            return exec_query<point_yk>("channel_id=" + std::to_string(channel_id));
        return exec_query<point_yk>("");
#else
        return {};
#endif
    }

    std::vector<point_yt> query_yt_points(int64_t channel_id) {
#ifdef EMS_DB_HAS_MYSQL
        if (channel_id > 0)
            return exec_query<point_yt>("channel_id=" + std::to_string(channel_id));
        return exec_query<point_yt>("");
#else
        return {};
#endif
    }

    // ---- 采集数据导入 ----

    int batch_import_collect_data(const std::vector<CollectImportData>& data_list) {
#ifdef EMS_DB_HAS_MYSQL
        if (data_list.empty()) return 0;

        std::vector<point_collect> entities;
        entities.reserve(data_list.size());
        for (const auto& d : data_list) {
            point_collect e;
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

std::vector<device> DbManager::query_all_devices() {
    return impl_->query_all_devices();
}

device DbManager::query_device_by_id(int64_t id) {
    return impl_->query_device_by_id(id);
}

device DbManager::query_device_by_code(const std::string& code) {
    return impl_->query_device_by_code(code);
}

bool DbManager::insert_device(const device& device) {
    return impl_->insert_device(device);
}

bool DbManager::update_device(const device& device) {
    return impl_->update_device(device);
}

bool DbManager::delete_device(int64_t id) {
    return impl_->delete_device(id);
}

// ---- 通道 ----

std::vector<channel> DbManager::query_all_channels() {
    return impl_->query_all_channels();
}

std::vector<channel> DbManager::query_channels_by_device(int64_t device_id) {
    return impl_->query_channels_by_device(device_id);
}

channel DbManager::query_channel_by_id(int64_t id) {
    return impl_->query_channel_by_id(id);
}

channel DbManager::query_channel_by_code(const std::string& code) {
    return impl_->query_channel_by_code(code);
}

bool DbManager::insert_channel(const channel& channel) {
    return impl_->insert_channel(channel);
}

bool DbManager::update_channel(const channel& channel) {
    return impl_->update_channel(channel);
}

bool DbManager::delete_channel(int64_t id) {
    return impl_->delete_channel(id);
}

// ---- 测点 ----

std::vector<point_yc> DbManager::query_yc_points(int64_t channel_id) {
    return impl_->query_yc_points(channel_id);
}

std::vector<point_yx> DbManager::query_yx_points(int64_t channel_id) {
    return impl_->query_yx_points(channel_id);
}

std::vector<point_yk> DbManager::query_yk_points(int64_t channel_id) {
    return impl_->query_yk_points(channel_id);
}

std::vector<point_yt> DbManager::query_yt_points(int64_t channel_id) {
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

// ============================================================
// 数据库初始化（自动建库建表）
// ============================================================

static const char* DB_INIT_SQL = R"sql(
CREATE DATABASE IF NOT EXISTS __DBNAME__
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_unicode_ci;
USE __DBNAME__;

		CREATE TABLE IF NOT EXISTS device (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    code VARCHAR(64) NOT NULL DEFAULT '',
    name VARCHAR(128) NOT NULL DEFAULT '',
    device_type INT NOT NULL DEFAULT 0,
    group_id BIGINT NOT NULL DEFAULT 0,
    description VARCHAR(256) DEFAULT '',
    enable INT NOT NULL DEFAULT 1,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

		CREATE TABLE IF NOT EXISTS channel (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    code VARCHAR(64) NOT NULL DEFAULT '',
    name VARCHAR(128) NOT NULL DEFAULT '',
    device_id BIGINT NOT NULL DEFAULT 0,
    protocol_type INT NOT NULL DEFAULT 1,
    conn_type INT NOT NULL DEFAULT 2,
    ip VARCHAR(64) DEFAULT '127.0.0.1',
    port INT DEFAULT 502,
    com_port VARCHAR(64) DEFAULT '',
    baud_rate INT DEFAULT 9600,
    data_bits INT DEFAULT 8,
    stop_bits INT DEFAULT 1,
    parity VARCHAR(8) DEFAULT 'N',
    rtu_addr VARCHAR(16) DEFAULT '1',
    timeout INT DEFAULT 5,
    enable INT NOT NULL DEFAULT 1,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

		CREATE TABLE IF NOT EXISTS point_yc (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    code VARCHAR(128) NOT NULL DEFAULT '',
    name VARCHAR(128) NOT NULL DEFAULT '',
    channel_id BIGINT NOT NULL DEFAULT 0,
    device_id BIGINT NOT NULL DEFAULT 0,
    protocol_type INT DEFAULT 1,
    func_code INT DEFAULT 3,
    slave_id INT DEFAULT 1,
    reg_addr INT DEFAULT 0,
    bit_position INT DEFAULT 0,
    data_id BIGINT DEFAULT 0,
    casdu INT DEFAULT 0,
    ioa BIGINT DEFAULT 0,
    object_ref VARCHAR(128) DEFAULT '',
    decode_code INT DEFAULT 0,
    endian INT DEFAULT 0,
    mul_coe DOUBLE DEFAULT 1.0,
    add_coe DOUBLE DEFAULT 0.0,
    min_limit DOUBLE DEFAULT -9999999.0,
    max_limit DOUBLE DEFAULT 9999999.0,
    unit VARCHAR(32) DEFAULT '',
    enable INT DEFAULT 1,
    description VARCHAR(256) DEFAULT '',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS point_yx (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    code VARCHAR(128) NOT NULL DEFAULT '',
    name VARCHAR(128) NOT NULL DEFAULT '',
    channel_id BIGINT NOT NULL DEFAULT 0,
    device_id BIGINT NOT NULL DEFAULT 0,
    protocol_type INT DEFAULT 1,
    func_code INT DEFAULT 1,
    slave_id INT DEFAULT 1,
    reg_addr INT DEFAULT 0,
    bit_position INT DEFAULT 0,
    data_id BIGINT DEFAULT 0,
    casdu INT DEFAULT 0,
    ioa BIGINT DEFAULT 0,
    object_ref VARCHAR(128) DEFAULT '',
    decode_code INT DEFAULT 0,
    endian INT DEFAULT 0,
    bit INT DEFAULT 0,
    reverse_flag INT DEFAULT 0,
    enable INT DEFAULT 1,
    description VARCHAR(256) DEFAULT '',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS point_yk (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    code VARCHAR(128) NOT NULL DEFAULT '',
    name VARCHAR(128) NOT NULL DEFAULT '',
    channel_id BIGINT NOT NULL DEFAULT 0,
    device_id BIGINT NOT NULL DEFAULT 0,
    protocol_type INT DEFAULT 1,
    func_code INT DEFAULT 5,
    slave_id INT DEFAULT 1,
    reg_addr INT DEFAULT 0,
    bit_position INT DEFAULT 0,
    data_id BIGINT DEFAULT 0,
    casdu INT DEFAULT 0,
    ioa BIGINT DEFAULT 0,
    object_ref VARCHAR(128) DEFAULT '',
    decode_code INT DEFAULT 0,
    endian INT DEFAULT 0,
    bit INT DEFAULT 0,
    command_type INT DEFAULT 0,
    related_yx_id BIGINT DEFAULT 0,
    enable INT DEFAULT 1,
    description VARCHAR(256) DEFAULT '',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS point_yt (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    code VARCHAR(128) NOT NULL DEFAULT '',
    name VARCHAR(128) NOT NULL DEFAULT '',
    channel_id BIGINT NOT NULL DEFAULT 0,
    device_id BIGINT NOT NULL DEFAULT 0,
    protocol_type INT DEFAULT 1,
    func_code INT DEFAULT 6,
    slave_id INT DEFAULT 1,
    reg_addr INT DEFAULT 0,
    bit_position INT DEFAULT 0,
    data_id BIGINT DEFAULT 0,
    casdu INT DEFAULT 0,
    ioa BIGINT DEFAULT 0,
    object_ref VARCHAR(128) DEFAULT '',
    decode_code INT DEFAULT 0,
    endian INT DEFAULT 0,
    mul_coe DOUBLE DEFAULT 1.0,
    add_coe DOUBLE DEFAULT 0.0,
    min_limit DOUBLE DEFAULT -9999999.0,
    max_limit DOUBLE DEFAULT 9999999.0,
    unit VARCHAR(32) DEFAULT '',
    enable INT DEFAULT 1,
    description VARCHAR(256) DEFAULT '',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

		CREATE TABLE IF NOT EXISTS point_collect (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    point_code VARCHAR(128) NOT NULL DEFAULT '',
    point_type INT DEFAULT 0,
    channel_id BIGINT DEFAULT 0,
    device_id BIGINT DEFAULT 0,
    raw_value BIGINT DEFAULT 0,
    eng_value DOUBLE DEFAULT 0.0,
    quality INT DEFAULT 0,
    collect_time DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
)sql";

static bool exec_sql(const DbConfig& config, const std::string& db,
                      const std::string& sql) {
    // 写入临时文件执行，避免 shell 转义和命令行长度限制
    std::string tmpfile = "/tmp/ems_db_init_" + std::to_string(getpid()) + ".sql";
    {
        std::ofstream ofs(tmpfile);
        if (!ofs) return false;
        ofs << sql;
    }

    std::string cmd = "mysql -h" + config.host + " -P" + std::to_string(config.port)
                    + " -u" + config.user + " -p'" + config.password + "'"
                    + " " + db + " < " + tmpfile + " 2>/dev/null";
    int rc = system(cmd.c_str());
    std::remove(tmpfile.c_str());
    return rc == 0;
}

static std::string replace_dbname(const std::string& sql, const std::string& dbname) {
    std::string r = sql;
    auto pos = r.find("__DBNAME__");
    while (pos != std::string::npos) {
        r.replace(pos, 10, dbname);
        pos = r.find("__DBNAME__", pos + dbname.size());
    }
    return r;
}

bool ensure_database(const DbConfig& config) {
    auto sql = replace_dbname(DB_INIT_SQL, config.dbname);
    // 先不指定库，建库后再建表
    std::string createDb = "CREATE DATABASE IF NOT EXISTS " + config.dbname
                         + " DEFAULT CHARACTER SET utf8mb4;";
    if (!exec_sql(config, "", createDb)) return false;

    // 拆成多个 CREATE TABLE 语句逐一执行
    std::vector<std::string> stmts;
    size_t pos = 0;
    while ((pos = sql.find("CREATE TABLE", pos)) != std::string::npos) {
        size_t end = sql.find(';', pos);
        if (end != std::string::npos) {
            stmts.push_back(sql.substr(pos, end - pos + 1));
            pos = end + 1;
        } else {
            stmts.push_back(sql.substr(pos));
            break;
        }
    }

    for (const auto& stmt : stmts) {
        if (!exec_sql(config, config.dbname, stmt)) return false;
    }
    return true;
}

bool ensure_database_with_seed(const DbConfig& config) {
    if (!ensure_database(config)) return false;

    const char* seed = R"sql(
INSERT IGNORE INTO device (id, code, name, device_type, enable)
VALUES (1, 'PCS_001', 'PCS-1 储能变流器', 11, 1);
INSERT IGNORE INTO channel (id, code, name, device_id, protocol_type, conn_type,
    ip, port, rtu_addr, timeout, enable)
VALUES (1, 'CH_MODBUS_001', 'Modbus-TCP-1', 1, 1, 2, '127.0.0.1', 502, '1', 5, 1);
INSERT IGNORE INTO point_yc (id, code, name, channel_id, device_id, slave_id,
    func_code, reg_addr, decode_code, endian, unit, enable) VALUES
(1,'totalActivePower','总有功功率',1,1,1,3,0,4,0,'kW',1),
(2,'totalReactivePower','总无功功率',1,1,1,3,2,4,0,'kVar',1),
(3,'phaseAVoltage','A相电压',1,1,1,3,4,4,0,'V',1),
(4,'phaseBVoltage','B相电压',1,1,1,3,6,4,0,'V',1),
(5,'phaseCVoltage','C相电压',1,1,1,3,8,4,0,'V',1),
(6,'phaseACurrent','A相电流',1,1,1,3,10,4,0,'A',1),
(7,'phaseBCurrent','B相电流',1,1,1,3,12,4,0,'A',1),
(8,'phaseCCurrent','C相电流',1,1,1,3,14,4,0,'A',1),
(9,'gridFrequency','电网频率',1,1,1,3,16,4,0,'Hz',1),
(10,'soc','SOC',1,1,1,3,18,4,0,'%',1);
INSERT IGNORE INTO point_yx (id, code, name, channel_id, device_id, slave_id,
    func_code, reg_addr, decode_code, endian, enable) VALUES
(1,'runningStatus','运行状态',1,1,1,1,0,8,0,1),
(2,'faultAlarm','故障告警',1,1,1,1,0,8,0,1),
(3,'commStatus','通信状态',1,1,1,1,0,8,0,1);
)sql";
    return exec_sql(config, config.dbname, seed);
}

} // namespace db
} // namespace ems
