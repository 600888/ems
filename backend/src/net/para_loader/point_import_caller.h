/**
 * @file point_import_caller.h
 * @brief net 模块调用的测点采集数据导入接口
 *
 * 在 Modbus 采集线程中调用，将读取到的测点数据存入关系数据库。
 */
#pragma once

#include <string>
#include <vector>
#include <memory>
#include "db/db_manager.h"
#include "db/point_import.h"

namespace ems {
namespace net {
namespace device {

/**
 * @brief 采集数据导入调用器
 *
 * 提供给 net 模块协议处理器使用，将采集线程读取到的
 * 测点值批量导入关系数据库持久化。
 */
class PointImportCaller {
public:
    PointImportCaller();
    ~PointImportCaller();

    /**
     * @brief 初始化 (建立数据库连接)
     * @param config 数据库配置
     * @return true 成功
     */
    bool initialize(const ems::db::DbConfig& config);

    /**
     * @brief 批量导入读取到的测点采集数据
     * @param data_list 采集数据列表
     * @return >=0 成功导入条数, -1 失败
     */
    int import_collected(const std::vector<ems::db::CollectImportData>& data_list);

    /**
     * @brief 批量导入 YC 采集数据 (便捷接口)
     * @param channel_id  通道ID
     * @param device_id   设备ID
     * @param values      测点值列表 {point_code, raw_value, eng_value}
     * @param collect_time 采集时间
     */
    int import_yc_batch(
        int64_t channel_id,
        int64_t device_id,
        const std::vector<std::tuple<std::string, int64_t, double>>& values,
        const std::string& collect_time);

    /**
     * @brief 批量导入 YX 采集数据 (便捷接口)
     */
    int import_yx_batch(
        int64_t channel_id,
        int64_t device_id,
        const std::vector<std::pair<std::string, int>>& values,
        const std::string& collect_time);

    /**
     * @brief 是否已初始化
     */
    bool is_initialized() const { return initialized_; }

private:
    bool initialized_ = false;
    std::unique_ptr<ems::db::DbManager>  dbm_;
    std::unique_ptr<ems::db::PointImporter> importer_;
};

} // namespace device
} // namespace net
} // namespace ems
