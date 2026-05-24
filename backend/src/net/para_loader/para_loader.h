/**
 * @file para_loader.h
 * @brief 采集进程参数加载器 - 从关系数据库加载设备/通道/测点配置
 *
 * 采集进程启动时调用本类从关系数据库加载所有运行参数，
 * 包括设备清单、通道连接参数、测点映射表等，初始化到运行时配置。
 *
 * 与 PointImportCaller 的区别:
 *   ParaLoader 负责  DB -> 运行时配置  (启动时加载)
 *   PointImportCaller 负责 采集数据 -> DB (运行时定时写入)
 */
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "db/db_manager.h"
#include "rtdb/rtdb_channel.h"
#include "rtdb/rtdb_device.h"

namespace ems {
namespace net {
namespace device {

/**
 * @brief 参数加载器
 *
 * 采集进程启动时调用 load_all() 一次性加载全量配置，
 * 或按需调用 load_devices() / load_channels() / load_yc_points() 等。
 *
 * 用法:
 * @code
 *   ParaLoader loader;
 *   loader.initialize(config);
 *
 *   auto devices = loader.load_devices();      // 所有启用的设备
 *   auto chs     = loader.load_channels(dev_id); // 某设备下的通道
 *
 *   // 或一次性加载并转换为运行时配置
 *   auto all = loader.load_all_as_config();
 * @endcode
 */
class ParaLoader {
public:
    ParaLoader();
    ~ParaLoader();

    // ---- 拷贝/赋值 禁用 ----
    ParaLoader(const ParaLoader&) = delete;
    ParaLoader& operator=(const ParaLoader&) = delete;
    ParaLoader(ParaLoader&&) noexcept = default;
    ParaLoader& operator=(ParaLoader&&) noexcept = default;

    /**
     * @brief 初始化数据库连接
     * @param config 数据库配置
     * @return true 成功
     */
    bool initialize(const ems::db::DbConfig& config);

    // ============================================================
    // 设备加载
    // ============================================================

    /**
     * @brief 从 DB 加载所有启用设备 (enable=1)
     * @return 设备实体列表
     */
    std::vector<ems::db::device> load_devices();

    /**
     * @brief 按 ID 加载设备
     */
    ems::db::device load_device_by_id(int64_t id);

    /**
     * @brief 按编码加载设备
     */
    ems::db::device load_device_by_code(const std::string& code);

    // ============================================================
    // 通道加载
    // ============================================================

    /**
     * @brief 从 DB 加载所有启用通道 (enable=1)
     * @param device_id 可选: 按设备过滤, 0=全部
     * @return 通道实体列表
     */
    std::vector<ems::db::channel> load_channels(int64_t device_id = 0);

    /**
     * @brief 按 ID 加载通道
     */
    ems::db::channel load_channel_by_id(int64_t id);

    // ============================================================
    // 测点配置加载
    // ============================================================

    /**
     * @brief 加载 YC (遥测) 测点配置
     * @param channel_id 可选: 按通道过滤, 0=全部
     */
    std::vector<ems::db::point_yc> load_yc_points(int64_t channel_id = 0);

    /**
     * @brief 加载 YX (遥信) 测点配置
     */
    std::vector<ems::db::point_yx> load_yx_points(int64_t channel_id = 0);

    /**
     * @brief 加载 YK (遥控) 测点配置
     */
    std::vector<ems::db::point_yk> load_yk_points(int64_t channel_id = 0);

    /**
     * @brief 加载 YT (遥调) 测点配置
     */
    std::vector<ems::db::point_yt> load_yt_points(int64_t channel_id = 0);

    // ============================================================
    // 运行时配置转换 (DB entity -> 运行时 config)
    // ============================================================

    /**
     * @brief 将 device 列表转换为运行时 DeviceConfig 列表
     *
     * 自动关联通道到设备、以及空测点列表 (需后续加载)。
     */
    std::vector<ems::rtdb::DeviceConfig> to_device_configs(
        const std::vector<ems::db::device>& devices,
        const std::vector<ems::db::channel>& channels = {});

    /**
     * @brief 将 channel 列表转换为运行时 ChannelConfig 列表
     */
    std::vector<ems::rtdb::ChannelConfig> to_channel_configs(
        const std::vector<ems::db::channel>& channels);

    /**
     * @brief 单条转换
     */
    ems::rtdb::ChannelConfig to_channel_config(const ems::db::channel& ch);

    /**
     * @brief 一次性加载全部配置并转为运行时配置
     *
     * @param[out] devices  运行时设备列表
     * @param[out] channels 运行时通道列表
     * @param[out] yc_points YC 测点实体 (可选, 可为 nullptr)
     * @param[out] yx_points YX 测点实体 (可选)
     * @param[out] yk_points YK 测点实体 (可选)
     * @param[out] yt_points YT 测点实体 (可选)
     * @return true 加载成功
     */
    bool load_all_as_config(
        std::vector<ems::rtdb::DeviceConfig>& devices,
        std::vector<ems::rtdb::ChannelConfig>& channels,
        std::vector<ems::db::point_yc>* yc_points = nullptr,
        std::vector<ems::db::point_yx>* yx_points = nullptr,
        std::vector<ems::db::point_yk>* yk_points = nullptr,
        std::vector<ems::db::point_yt>* yt_points = nullptr);

    /**
     * @brief 是否已初始化
     */
    bool is_initialized() const { return initialized_; }

    /**
     * @brief 获取最近一次错误信息
     */
    std::string last_error() const { return last_err_; }

private:
    bool initialized_ = false;
    std::shared_ptr<ems::db::DbManager> dbm_;
    std::string last_err_;
};

} // namespace device
} // namespace net
} // namespace ems
