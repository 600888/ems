/**
 * @file db_manager.h
 * @brief 数据库连接管理器 (封装 ormpp 操作)
 *
 * 提供数据库连接、设备/通道/测点的 CRUD、批量导入等接口。
 * 编译为动态库 (libems_db.so) 供各模块调用。
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "channel.h"
#include "device.h"
#include "point.h"

namespace ems {
namespace db {

/**
 * @brief 数据库连接配置
 */
struct DbConfig {
    std::string host = "127.0.0.1";
    int port = 3306;
    std::string user = "root";
    std::string password = "";
    std::string dbname = "ems";
    int timeout = 5;
    int conn_num = 4;
};

/**
 * @brief 采集数据导入结构
 */
struct CollectImportData {
    std::string point_code;
    int point_type;  // 0=YC,1=YX,2=YK,3=YT
    int64_t channel_id;
    int64_t device_id;
    int64_t raw_value;         // 原始报文值
    double eng_value;          // 工程变换值
    int quality;               // 数据品质
    std::string collect_time;  // 采集时间 "2006-01-02 15:04:05"
};

/**
 * @brief 数据库管理器
 *
 * 封装 ormpp 的 dbng<mysql> 操作，提供便捷的 CRUD 和批量导入接口。
 * 线程安全：每个线程应持有独立实例或外部同步。
 */
class DbManager {
   public:
    DbManager();
    ~DbManager();

    // ---- 删除拷贝/赋值 ----
    DbManager(const DbManager&) = delete;
    DbManager& operator=(const DbManager&) = delete;
    DbManager(DbManager&&) noexcept;
    DbManager& operator=(DbManager&&) noexcept;

    // ============================================================
    // 连接管理
    // ============================================================

    /**
     * @brief 连接 MySQL 数据库
     */
    bool connect(const DbConfig& config);

    /**
     * @brief 连接 SQLite 数据库
     * @param db_path 数据库文件路径
     */
    bool connect_sqlite(const std::string& db_path);

    /**
     * @brief 断开连接
     */
    void disconnect();

    /**
     * @brief 是否已连接
     */
    bool is_connected() const;

    /**
     * @brief 获取最后一次错误信息
     */
    std::string last_error() const;

    // ============================================================
    // 设备操作
    // ============================================================

    std::vector<device> query_all_devices();
    device query_device_by_id(int64_t id);
    device query_device_by_code(const std::string& code);
    bool insert_device(const device& device);
    bool update_device(const device& device);
    bool delete_device(int64_t id);

    // ============================================================
    // 通道操作
    // ============================================================

    std::vector<channel> query_all_channels();
    std::vector<channel> query_channels_by_device(int64_t device_id);
    channel query_channel_by_id(int64_t id);
    channel query_channel_by_code(const std::string& code);
    bool insert_channel(const channel& channel);
    bool update_channel(const channel& channel);
    bool delete_channel(int64_t id);

    // ============================================================
    // 测点配置操作
    // ============================================================

    std::vector<point_yc> query_yc_points(int64_t channel_id = 0);
    std::vector<point_yx> query_yx_points(int64_t channel_id = 0);
    std::vector<point_yk> query_yk_points(int64_t channel_id = 0);
    std::vector<point_yt> query_yt_points(int64_t channel_id = 0);

    // ============================================================
    // 采集数据导入
    // ============================================================

    /**
     * @brief 批量导入测点采集数据
     * @param data_list 采集数据列表
     * @return 成功导入条数, -1 表示失败
     */
    int batch_import_collect_data(const std::vector<CollectImportData>& data_list);

   private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief 获取默认数据库配置
 *
 * 优先级: 环境变量 > db.json 配置文件 > 内置默认值
 *
 * db.json 路径:
 *   1. 环境变量 EMS_DB_CONF 指定的路径
 *   2. 默认 /usr/ems/conf/db.json
 *
 * 环境变量 (覆盖配置文件):
 *   EMS_DB_HOST, EMS_DB_PORT, EMS_DB_USER,
 *   EMS_DB_PASS, EMS_DB_NAME
 *
 * @code{.json}
 * {
 *     "host": "127.0.0.1",
 *     "port": 3306,
 *     "user": "root",
 *     "password": "",
 *     "dbname": "ems",
 *     "timeout": 5,
 *     "conn_num": 4
 * }
 * @endcode
 */
DbConfig get_default_db_config();

/**
 * @brief 初始化数据库（自动建库建表）
 *
 * 如果数据库不存在则创建，如果表不存在则创建。
 * 需要 MySQL 服务器运行中，且提供有 CREATE 权限的账号。
 * 内部使用 MYSQL C API 连接 MySQL（不依赖 ormpp），
 * 仅当 libmysqlclient-dev 可用时生效。
 *
 * @param config 数据库连接参数（使用其中的 host/port/user/password，dbname 为目标库名）
 * @return true 成功（数据库和表已就绪）
 */
bool ensure_database(const DbConfig& config);

/**
 * @brief 初始化数据库并写入示例测点
 *
 * 在 ensure_database 基础上，如果设备表和通道表为空，
 * 写入一组默认的 Modbus 测点配置用于测试。
 *
 * @return true 成功
 */
bool ensure_database_with_seed(const DbConfig& config);

}  // namespace db
}  // namespace ems
