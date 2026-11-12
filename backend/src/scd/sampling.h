#pragma once

#include <common/database/mysql_database.h>
#include <common/database/timeseries/store.h>

#include <functional>
#include <optional>

namespace ems::storage {
constexpr std::int64_t kDefaultArchiveIntervalMs = 5000;

struct ArchivePoint {
    database::SeriesMapping series;
    std::string point_type;
    std::string scope_kind;
    std::string scope_id;
    bool enabled{true};
    bool unsigned_integer{false};
    std::int64_t stale_after_ms{60000};
};

struct ArchiveDirectory {
    std::string station_id;
    std::int64_t config_revision{0};
    std::vector<ArchivePoint> points;
};

struct ArchiveReport {
    std::size_t written{0};
    std::size_t invalid{0};
    std::size_t failed{0};
    bool duplicate_slot{false};
};

/**
 * @brief 从当前四遥配置构造归档目录；子表名按站根至测点的编码以冒号连接。
 * @param[in] records 同一配置事务读取的全量记录。
 * @return 四遥归档目录；编码非法、路径重复或超长、重复身份、孤立节点、环及未知值类型返回错误。
 */
core::Result<ArchiveDirectory> build_archive_directory(
    const database::DeviceConfigRecords& records);

class ArchiveDirectorySource {
   public:
    /**
     * @brief 创建按站根 config_revision 热加载的目录源。
     * @param[in] database 配置及稳定时序映射仓储上下文，共享所有权。
     */
    explicit ArchiveDirectorySource(database::MySqlDatabaseContext database);
    /**
     * @brief 比对修订号，必要时重建目录并原子登记稳定时序映射。
     * @param[in] ctx 配置读取和映射事务的调用期限。
     * @return 当前目录；失败时不发布半成品，调用方应跳过本次采样。
     */
    core::Result<ArchiveDirectory> refresh(const core::CallContext& ctx);

   private:
    database::MySqlDatabaseContext database_;
    std::optional<ArchiveDirectory> directory_;
};

class PeriodicArchiveSampler {
   public:
    using ReadPoint = std::function<core::Result<std::string>(
        const std::string&, const std::string&, const core::CallContext&)>;
    /**
     * @brief 创建定时采样器；读取端口和时序库生命周期由调用方保持。
     * @param[in] reader 实时值读取端口，不执行现场命令。
     * @param[in,out] series 公共时序写入端口。
     */
    PeriodicArchiveSampler(ReadPoint reader, database::ITimeSeriesStore& series);
    /**
     * @brief 归档所有配置点位当前状态；缺失、禁用、陈旧值以 null 和质量原因记录。
     * @param[in] directory 当前配置目录。
     * @param[in] slot_us 本次实际采样槽的 Unix 微秒，不回填错过的槽。
     * @param[in] ctx 本次采样的总期限。
     * @return 真实写入、无效及失败计数；同一采样器重复或倒退的槽不再写入。
     */
    ArchiveReport sample(const ArchiveDirectory& directory, std::int64_t slot_us,
                         const core::CallContext& ctx);

   private:
    ReadPoint reader_;
    database::ITimeSeriesStore& series_;
    std::int64_t last_slot_us_{0};
};
}  // namespace ems::storage
