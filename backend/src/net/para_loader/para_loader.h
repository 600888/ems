/**
 * @file para_loader.h
 * @brief 采集进程参数加载器 - 从关系数据库加载设备/通道/测点配置
 *
 * 采集进程启动时调用本类从关系数据库加载所有运行参数，
 * 包括设备清单、通道连接参数、测点映射表等，初始化到运行时配置。
 *
 * 公共仓储负责数据库读取，本类保留已成功加载的配置用于运行时转换。
 */
#pragma once

#include <common/database/mysql_database.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rtdb/model/rtdb_channel.h"
#include "rtdb/model/rtdb_device.h"

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
    /** @brief 构造 ParaLoader 实例。 */
    ParaLoader();
    /** @brief 释放 ParaLoader 持有的资源。 */
    ~ParaLoader();

    // ---- 拷贝/赋值 禁用 ----
    /**
     * @brief 禁止复制或移动该对象。
     * @param[in] other 待复制或移动的源对象。
     */
    ParaLoader(const ParaLoader& other) = delete;
    /**
     * @brief 禁止复制赋值。
     * @param[in] other 待赋值的源对象。
     */
    ParaLoader& operator=(const ParaLoader& other) = delete;
    /**
     * @brief 构造 ParaLoader 实例。
     * @param[in] other 待复制或移动的源对象。
     */
    ParaLoader(ParaLoader&& other) noexcept = default;
    /**
     * @brief 执行复制或移动赋值。
     * @param[in] other 待赋值的源对象。
     * @return 当前对象的引用。
     */
    ParaLoader& operator=(ParaLoader&& other) noexcept = default;

    /**
     * @brief 注入进程入口创建的公共数据库上下文并加载配置。
     * @param[in] database 进程内共享数据库上下文。
     * @param[in] ctx 读取期限。
     * @return true 成功
     */
    bool initialize(ems::database::MySqlDatabaseContext database,
                    const ems::core::CallContext& ctx);

    /**
     * @brief 从公共仓储重新加载配置，失败保留上次成功数据并返回错误。
     * @param[in] ctx 读取期限。
     * @return true 表示全量读取成功，失败原因通过 last_error 获取。
     */
    bool reload(const ems::core::CallContext& ctx);

    // ============================================================
    // 设备加载
    // ============================================================

    /**
     * @brief 从 DB 加载所有启用设备 (enable=1)
     * @return 设备实体列表
     */
    std::vector<ems::database::DeviceRecord> load_devices();

    /**
     * @brief 按 ID 加载设备
     * @param[in] id 对象标识。
     * @return 得到的设备记录。
     */
    ems::database::DeviceRecord load_device_by_id(int64_t id);

    /**
     * @brief 按编码加载设备
     * @param[in] code 对象编码。
     * @return 得到的设备记录。
     */
    ems::database::DeviceRecord load_device_by_code(const std::string& code);

    // ============================================================
    // 通道加载
    // ============================================================

    /**
     * @brief 从 DB 加载所有启用通道 (enable=1)
     * @param[in] device_id 可选: 按设备过滤, 0=全部
     * @return 通道实体列表
     */
    std::vector<ems::database::ChannelRecord> load_channels(int64_t device_id = 0);

    /**
     * @brief 按 ID 加载通道
     * @param[in] id 对象标识。
     * @return 得到的通道记录。
     */
    ems::database::ChannelRecord load_channel_by_id(int64_t id);

    // ============================================================
    // 测点配置加载
    // ============================================================

    /**
     * @brief 加载 YC (遥测) 测点配置
     * @param[in] channel_id 可选: 按通道过滤, 0=全部
     * @return 匹配条件的数据集合。
     */
    std::vector<ems::database::AnalogPoint> load_yc_points(int64_t channel_id = 0);

    /**
     * @brief 加载 YX (遥信) 测点配置
     * @param[in] channel_id 通道 ID。
     * @return 匹配条件的数据集合。
     */
    std::vector<ems::database::StatusPoint> load_yx_points(int64_t channel_id = 0);

    /**
     * @brief 加载 YK (遥控) 测点配置
     * @param[in] channel_id 通道 ID。
     * @return 匹配条件的数据集合。
     */
    std::vector<ems::database::ControlPoint> load_yk_points(int64_t channel_id = 0);

    /**
     * @brief 加载 YT (遥调) 测点配置
     * @param[in] channel_id 通道 ID。
     * @return 匹配条件的数据集合。
     */
    std::vector<ems::database::SetpointPoint> load_yt_points(int64_t channel_id = 0);

    // ============================================================
    // 运行时配置转换 (DB entity -> 运行时 config)
    // ============================================================

    /**
     * @brief 将 device 列表转换为运行时 DeviceConfig 列表
     *
     * 自动关联通道到设备、以及空测点列表 (需后续加载)。
     * @param[in] devices 设备列表。
     * @param[in] channels 通道列表。
     * @return 匹配条件的数据集合。
     */
    std::vector<ems::rtdb::DeviceConfig> to_device_configs(
        const std::vector<ems::database::DeviceRecord>& devices,
        const std::vector<ems::database::ChannelRecord>& channels = {});

    /**
     * @brief 将 channel 列表转换为运行时 ChannelConfig 列表
     * @param[in] channels 通道列表。
     * @return 匹配条件的数据集合。
     */
    std::vector<ems::rtdb::ChannelConfig> to_channel_configs(
        const std::vector<ems::database::ChannelRecord>& channels);

    /**
     * @brief 单条转换
     * @param[in] ch 通道配置。
     * @return 得到的通道配置。
     */
    ems::rtdb::ChannelConfig to_channel_config(const ems::database::ChannelRecord& ch);

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
    bool load_all_as_config(std::vector<ems::rtdb::DeviceConfig>& devices,
                            std::vector<ems::rtdb::ChannelConfig>& channels,
                            std::vector<ems::database::AnalogPoint>* yc_points = nullptr,
                            std::vector<ems::database::StatusPoint>* yx_points = nullptr,
                            std::vector<ems::database::ControlPoint>* yk_points = nullptr,
                            std::vector<ems::database::SetpointPoint>* yt_points = nullptr);

    /**
     * @brief 是否已初始化
     * @return true 表示条件成立；false 表示不成立。
     */
    bool is_initialized() const { return initialized_; }

    /**
     * @brief 获取最近一次错误信息
     * @return 最近一次错误的说明文本。
     */
    std::string last_error() const { return last_err_; }

   private:
    bool initialized_ = false;
    ems::database::MySqlDatabaseContext database_;
    ems::database::ConfigurationRecords records_;
    std::string last_err_;
};

}  // namespace device
}  // namespace net
}  // namespace ems
