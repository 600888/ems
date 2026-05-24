/**
 * @file para_loader.cpp
 * @brief 采集进程参数加载器实现
 *
 * 封装对 DbManager 的查询操作，将数据库中的设备、通道、
 * 测点配置加载为运行时配置，供采集线程初始化使用。
 */
#include "para_loader.h"
#include <algorithm>
#include <unordered_map>

namespace ems {
namespace net {
namespace device {

ParaLoader::ParaLoader()
    : dbm_(std::make_shared<ems::db::DbManager>())
{
}

ParaLoader::~ParaLoader() = default;

bool ParaLoader::initialize(const ems::db::DbConfig& config) {
    if (initialized_) return true;

    bool ok = dbm_->connect(config);
    if (ok) {
        initialized_ = true;
    } else {
        last_err_ = dbm_->last_error();
    }
    return ok;
}

// ============================================================
// 设备加载
// ============================================================

std::vector<ems::db::device> ParaLoader::load_devices() {
    if (!initialized_) return {};

    auto devices = dbm_->query_all_devices();

    // 仅返回启用设备
    devices.erase(
        std::remove_if(devices.begin(), devices.end(),
            [](const ems::db::device& d) { return d.enable == 0; }),
        devices.end());

    return devices;
}

ems::db::device ParaLoader::load_device_by_id(int64_t id) {
    if (!initialized_) return {};
    auto dev = dbm_->query_device_by_id(id);
    if (dev.id == 0 || dev.enable == 0) return {};
    return dev;
}

ems::db::device ParaLoader::load_device_by_code(const std::string& code) {
    if (!initialized_) return {};
    auto dev = dbm_->query_device_by_code(code);
    if (dev.id == 0 || dev.enable == 0) return {};
    return dev;
}

// ============================================================
// 通道加载
// ============================================================

std::vector<ems::db::channel> ParaLoader::load_channels(int64_t device_id) {
    if (!initialized_) return {};

    std::vector<ems::db::channel> channels;
    if (device_id > 0) {
        channels = dbm_->query_channels_by_device(device_id);
    } else {
        channels = dbm_->query_all_channels();
    }

    // 仅返回启用通道
    channels.erase(
        std::remove_if(channels.begin(), channels.end(),
            [](const ems::db::channel& c) { return c.enable == 0; }),
        channels.end());

    return channels;
}

ems::db::channel ParaLoader::load_channel_by_id(int64_t id) {
    if (!initialized_) return {};
    auto ch = dbm_->query_channel_by_id(id);
    if (ch.id == 0 || ch.enable == 0) return {};
    return ch;
}

// ============================================================
// 测点配置加载
// ============================================================

std::vector<ems::db::point_yc> ParaLoader::load_yc_points(int64_t channel_id) {
    if (!initialized_) return {};
    return dbm_->query_yc_points(channel_id);
}

std::vector<ems::db::point_yx> ParaLoader::load_yx_points(int64_t channel_id) {
    if (!initialized_) return {};
    return dbm_->query_yx_points(channel_id);
}

std::vector<ems::db::point_yk> ParaLoader::load_yk_points(int64_t channel_id) {
    if (!initialized_) return {};
    return dbm_->query_yk_points(channel_id);
}

std::vector<ems::db::point_yt> ParaLoader::load_yt_points(int64_t channel_id) {
    if (!initialized_) return {};
    return dbm_->query_yt_points(channel_id);
}

// ============================================================
// 运行时配置转换
// ============================================================

ems::rtdb::ChannelConfig ParaLoader::to_channel_config(const ems::db::channel& ch) {
    ems::rtdb::ChannelConfig cfg;
    cfg.id_        = ch.id;
    cfg.name_      = ch.name;
    cfg.status_    = ch.enable;

    // protocol_type: 1=MODBUS_RTU, 2=MODBUS_TCP, 0=DLT645, 3=IEC104, 4=IEC61850
    cfg.protocol_  = static_cast<ems::model::ProtocolType>(ch.protocol_type);

    // 根据连接类型生成连接串
    if (ch.conn_type == 2) {
        // TCP
        cfg.connection_ = ch.ip + ":" + std::to_string(ch.port);
        cfg.baudRate_ = 0;
        cfg.dataBits_ = 0;
        cfg.stopBits_ = 0;
        cfg.parity_   = "none";
    } else {
        // RTU (串口)
        cfg.connection_ = ch.com_port;
        cfg.baudRate_ = ch.baud_rate;
        cfg.dataBits_ = ch.data_bits;
        cfg.stopBits_ = ch.stop_bits;
        cfg.parity_   = ch.parity;
    }

    // settings_ 可包含 rtu_addr, timeout 等扩展参数
    cfg.settings_ = "{"
        "\"rtu_addr\":\"" + ch.rtu_addr + "\","
        "\"timeout\":" + std::to_string(ch.timeout) +
    "}";

    return cfg;
}

std::vector<ems::rtdb::ChannelConfig> ParaLoader::to_channel_configs(
    const std::vector<ems::db::channel>& channels)
{
    std::vector<ems::rtdb::ChannelConfig> result;
    result.reserve(channels.size());
    for (const auto& ch : channels) {
        result.push_back(to_channel_config(ch));
    }
    return result;
}

std::vector<ems::rtdb::DeviceConfig> ParaLoader::to_device_configs(
    const std::vector<ems::db::device>& devices,
    const std::vector<ems::db::channel>& channels)
{
    std::vector<ems::rtdb::DeviceConfig> result;
    result.reserve(devices.size());

    // 按 device_id 分组建立通道索引
    std::unordered_map<int64_t, std::vector<int64_t>> ch_by_dev;
    for (const auto& ch : channels) {
        ch_by_dev[ch.device_id].push_back(ch.id);
    }

    for (const auto& dev : devices) {
        ems::rtdb::DeviceConfig cfg;
        cfg.id_          = dev.id;
        cfg.name_        = dev.name;
        cfg.deviceType_  = static_cast<ems::model::DeviceType>(dev.device_type);
        cfg.description_ = dev.description;
        cfg.status_      = dev.enable;
        cfg.createdAt_   = dev.created_at;
        cfg.updatedAt_   = dev.updated_at;

        // 关联通道
        auto it = ch_by_dev.find(dev.id);
        if (it != ch_by_dev.end()) {
            cfg.channelIds_ = it->second;
        }

        result.push_back(std::move(cfg));
    }

    return result;
}

bool ParaLoader::load_all_as_config(
    std::vector<ems::rtdb::DeviceConfig>& devices,
    std::vector<ems::rtdb::ChannelConfig>& channels,
    std::vector<ems::db::point_yc>* yc_points,
    std::vector<ems::db::point_yx>* yx_points,
    std::vector<ems::db::point_yk>* yk_points,
    std::vector<ems::db::point_yt>* yt_points)
{
    if (!initialized_) {
        last_err_ = "ParaLoader not initialized";
        return false;
    }

    // 1. 加载启用的设备和通道
    auto dev_entities = load_devices();
    auto ch_entities  = load_channels();

    // 2. 转换为运行时配置
    channels = to_channel_configs(ch_entities);
    devices  = to_device_configs(dev_entities, ch_entities);

    // 3. 加载测点配置 (全量)
    if (yc_points) *yc_points = load_yc_points();
    if (yx_points) *yx_points = load_yx_points();
    if (yk_points) *yk_points = load_yk_points();
    if (yt_points) *yt_points = load_yt_points();

    return true;
}

} // namespace device
} // namespace net
} // namespace ems
