/**
 * @file point_import.h
 * @brief 测点采集数据导入 API
 *
 * 提供便捷的采集数据导入接口，供 net 模块调用。
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "db/db_manager.h"

namespace ems {
namespace db {

/**
 * @brief 测点采集数据导入器
 *
 * 封装对 DbManager 的批量导入操作，
 * 提供更细粒度的导入策略 (全量替换 / 增量追加)。
 */
class PointImporter {
   public:
    /**
     * @brief 绑定负责写入采集数据的数据库管理器。
     * @param[in] dbm 数据库管理器。
     */
    explicit PointImporter(DbManager& dbm) : dbm_(dbm) {}

    /**
     * @brief 导入一批采集数据 (增量追加)
     * @param[in] data_list 采集数据列表
     * @return >=0 导入条数, -1 失败
     */
    int import_collected(const std::vector<CollectImportData>& data_list) {
        return dbm_.batch_import_collect_data(data_list);
    }

    /**
     * @brief 构建一条 YC 采集数据
     * @param[in] point_code 测点编码。
     * @param[in] channel_id 通道 ID。
     * @param[in] device_id 设备 ID。
     * @param[in] raw_value 原始采集值。
     * @param[in] eng_value 工程值。
     * @param[in] quality 测点品质码。
     * @param[in] collect_time 采集时间。
     * @return 得到的采集导入数据。
     */
    static CollectImportData make_yc_data(const std::string& point_code, int64_t channel_id,
                                          int64_t device_id, int64_t raw_value, double eng_value,
                                          int quality, const std::string& collect_time) {
        return {point_code, 0, channel_id, device_id, raw_value, eng_value, quality, collect_time};
    }

    /**
     * @brief 构建一条 YX 采集数据
     * @param[in] point_code 测点编码。
     * @param[in] channel_id 通道 ID。
     * @param[in] device_id 设备 ID。
     * @param[in] value 输入值。
     * @param[in] collect_time 采集时间。
     * @return 得到的采集导入数据。
     */
    static CollectImportData make_yx_data(const std::string& point_code, int64_t channel_id,
                                          int64_t device_id, int value,
                                          const std::string& collect_time) {
        return {point_code,
                1,
                channel_id,
                device_id,
                static_cast<int64_t>(value),
                static_cast<double>(value),
                0,
                collect_time};
    }

    /**
     * @brief 构建一条 YK 采集数据
     * @param[in] point_code 测点编码。
     * @param[in] channel_id 通道 ID。
     * @param[in] device_id 设备 ID。
     * @param[in] value 输入值。
     * @param[in] collect_time 采集时间。
     * @return 得到的采集导入数据。
     */
    static CollectImportData make_yk_data(const std::string& point_code, int64_t channel_id,
                                          int64_t device_id, int value,
                                          const std::string& collect_time) {
        return {point_code,
                2,
                channel_id,
                device_id,
                static_cast<int64_t>(value),
                static_cast<double>(value),
                0,
                collect_time};
    }

    /**
     * @brief 构建一条 YT 采集数据
     * @param[in] point_code 测点编码。
     * @param[in] channel_id 通道 ID。
     * @param[in] device_id 设备 ID。
     * @param[in] raw_value 原始采集值。
     * @param[in] eng_value 工程值。
     * @param[in] collect_time 采集时间。
     * @return 得到的采集导入数据。
     */
    static CollectImportData make_yt_data(const std::string& point_code, int64_t channel_id,
                                          int64_t device_id, int64_t raw_value, double eng_value,
                                          const std::string& collect_time) {
        return {point_code, 3, channel_id, device_id, raw_value, eng_value, 0, collect_time};
    }

   private:
    DbManager& dbm_;
};

}  // namespace db
}  // namespace ems
