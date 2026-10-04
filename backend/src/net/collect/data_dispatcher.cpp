/**
 * @file data_dispatcher.cpp
 * @brief 将 handler 的统一采集结果分发到实时库。
 */
#include "data_dispatcher.h"

#include <common/messaging/zmq_change_channel.h>
#include <spdlog/fmt/bundled/format.h>

#include <algorithm>
#include <chrono>
#include <sstream>
#include <unordered_map>

#include "net/log.h"
#include "net/protocol/protocol_types.h"
#include "rtdb/rtdb_manager.h"

namespace ems {
namespace net {
namespace collect {

namespace {
uint64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
}  // namespace

// ============================================================
// 构造 / 析构
// ============================================================

DataDispatcher::DataDispatcher() = default;

DataDispatcher::~DataDispatcher() { shutdown(); }

// ============================================================
// 初始化 / 关闭
// ============================================================

bool DataDispatcher::initialize(const DataDispatcherConfig& config) {
    if (initialized_.load()) {
        LOG_INFO("数据分发器已初始化, 跳过");
        return true;
    }

    LOG_INFO(fmt::format("初始化数据分发器: Redis={}:{}, db={}, 前缀={}", config.redisHost_,
                         config.redisPort_, config.redisDb_, config.keyPrefix_)
                 .c_str());
    config_ = config;

    ems::rtdb::RtDbConfig rtdbConfig;
    rtdbConfig.host = config.redisHost_;
    rtdbConfig.port = config.redisPort_;
    rtdbConfig.password = config.redisPassword_;
    rtdbConfig.db = config.redisDb_;
    rtdbConfig.poolSize = config.poolSize_;
    rtdbConfig.timeoutMs = config.timeoutMs_;
    rtdbConfig.prefix = config.keyPrefix_;

    rtdb_mgr_ = std::make_shared<ems::rtdb::RtDbManager>();
    if (!rtdb_mgr_->initialize(rtdbConfig)) {
        LOG_ERROR(fmt::format("RTDB管理器初始化失败: host={}, port={}", config.redisHost_,
                              config.redisPort_)
                      .c_str());
        rtdb_mgr_.reset();
        return false;
    }

    if (config.notificationBackend_ == DataDispatcherConfig::NotificationBackend::ZeroMQ) {
#ifdef EMS_WITH_ZEROMQ
        ems::messaging::ZmqChangeConfig zmqConfig;
        zmqConfig.endpoint = config.zmqEndpoint_;
        zmqConfig.max_queue_items = config.zmqQueueItems_;
        zmqConfig.max_message_bytes = config.zmqMaxMessageBytes_;
        zmqConfig.high_water_mark = config.zmqHighWaterMark_;
        zmq_publisher_ = std::make_shared<ems::messaging::ZmqChangePublisher>();
        if (!zmq_publisher_->start(zmqConfig)) {
            LOG_ERROR("ZeroMQ 变更通知端点绑定失败");
            zmq_publisher_.reset();
            rtdb_mgr_->shutdown();
            rtdb_mgr_.reset();
            return false;
        }
#else
        LOG_ERROR("当前构建未启用 EMS_WITH_ZEROMQ");
        rtdb_mgr_->shutdown();
        rtdb_mgr_.reset();
        return false;
#endif
    }

    initialized_.store(true);
    LOG_INFO("数据分发器初始化成功");
    return true;
}

void DataDispatcher::shutdown() {
    LOG_INFO("关闭数据分发器");
    initialized_.store(false);
#ifdef EMS_WITH_ZEROMQ
    if (zmq_publisher_) {
        zmq_publisher_->stop();
        zmq_publisher_.reset();
    }
#endif
    if (rtdb_mgr_) {
        rtdb_mgr_->shutdown();
        rtdb_mgr_.reset();
    }
}

std::shared_ptr<ems::rtdb::RtDbManager> DataDispatcher::rtdb_manager() const { return rtdb_mgr_; }

bool DataDispatcher::is_connected() const { return rtdb_mgr_ && rtdb_mgr_->isConnected(); }

// ============================================================
// 单点更新
// ============================================================

bool DataDispatcher::update_yc(const std::string& pointCode, double value, int quality,
                               uint64_t timestamp) {
    if (!initialized_.load() || !rtdb_mgr_) return false;

    auto point = make_yc_point(pointCode, value, quality, timestamp);
    bool ok = rtdb_mgr_->updateYc(pointCode, point);

    {
        std::lock_guard lock(stats_mutex_);
        stats_.totalUpdates++;
        if (!ok) stats_.failedUpdates++;
    }

    if (ok) {
        publish_change("YC", point.to_json().dump());
    }
    notify_callback("YC", pointCode, ok);
    return ok;
}

bool DataDispatcher::update_yx(const std::string& pointCode, int value, int quality,
                               uint64_t timestamp) {
    if (!initialized_.load() || !rtdb_mgr_) return false;

    auto point = make_yx_point(pointCode, value, quality, timestamp);
    bool ok = rtdb_mgr_->updateYx(pointCode, point);

    {
        std::lock_guard lock(stats_mutex_);
        stats_.totalUpdates++;
        if (!ok) stats_.failedUpdates++;
    }

    if (ok) {
        publish_change("YX", point.to_json().dump());
    }
    notify_callback("YX", pointCode, ok);
    return ok;
}

bool DataDispatcher::update_yk(const std::string& pointCode, int value, int quality, int ykStatus,
                               uint64_t timestamp) {
    if (!initialized_.load() || !rtdb_mgr_) return false;

    model::YkPoint point;
    point.code_ = pointCode;
    point.name_ = pointCode;
    point.value_ = value;
    point.status_ = static_cast<model::YKStatus>(ykStatus);
    point.timestamp_ = timestamp;

    bool ok = rtdb_mgr_->updateYk(pointCode, point);

    {
        std::lock_guard lock(stats_mutex_);
        stats_.totalUpdates++;
        if (!ok) stats_.failedUpdates++;
    }

    if (ok) {
        publish_change("YK", point.to_json().dump());
    }
    notify_callback("YK", pointCode, ok);
    return ok;
}

bool DataDispatcher::update_yt(const std::string& pointCode, double value, int quality,
                               uint64_t timestamp) {
    if (!initialized_.load() || !rtdb_mgr_) return false;

    model::YtPoint point;
    point.code_ = pointCode;
    point.value_ = value;
    point.timestamp_ = timestamp;
    point.quality_ = static_cast<model::PointQuality>(quality);

    bool ok = rtdb_mgr_->updateYt(pointCode, point);

    {
        std::lock_guard lock(stats_mutex_);
        stats_.totalUpdates++;
        if (!ok) stats_.failedUpdates++;
    }

    if (ok) {
        publish_change("YT", point.to_json().dump());
    }
    notify_callback("YT", pointCode, ok);
    return ok;
}

// ============================================================
// 批量更新
// ============================================================

int DataDispatcher::batch_update(const std::vector<PointValue>& results) {
    if (!initialized_.load() || !rtdb_mgr_) {
        LOG_ERROR("数据分发器未初始化, 无法批量更新");
        return enum_value(protocol::OperationResult::Failure);
    }

    std::map<std::string, model::YcPoint> ycBatch;
    std::map<std::string, model::YxPoint> yxBatch;

    for (const auto& r : results) {
        // 根据测点编码前缀判断类型 (约定: YC_/YX_/YK_/YT_ 或包含特定标识)
        if (r.code.rfind("YC_", 0) == 0 || r.code.rfind("yc_", 0) == 0) {
            ycBatch[r.code] = make_yc_point(r.code, r.value, r.quality, r.timestamp);
        } else if (r.code.rfind("YX_", 0) == 0 || r.code.rfind("yx_", 0) == 0) {
            yxBatch[r.code] =
                make_yx_point(r.code, static_cast<int>(r.value), r.quality, r.timestamp);
        } else {
            // 无前缀，默认当作 YC
            ycBatch[r.code] = make_yc_point(r.code, r.value, r.quality, r.timestamp);
        }
    }

    int total = 0;
    if (!ycBatch.empty()) {
        int n = batch_update_yc(ycBatch);
        total += (n >= 0) ? n : 0;
        LOG_DEBUG(fmt::format("批量更新YC: {}点, 成功{}点", ycBatch.size(), total).c_str());
    }
    if (!yxBatch.empty()) {
        int n = batch_update_yx(yxBatch);
        total += (n >= 0) ? n : 0;
        LOG_DEBUG(
            fmt::format("批量更新YX: {}点, 成功{}点", yxBatch.size(), n >= 0 ? n : 0).c_str());
    }

    return total;
}

int DataDispatcher::batch_update_yc(const std::map<std::string, model::YcPoint>& points) {
    if (!initialized_.load() || !rtdb_mgr_ || points.empty()) return 0;

    // 分批写入（防止一次写入数据量过大）
    int updated = 0;
    std::map<std::string, model::YcPoint> batch;
    for (const auto& [code, pt] : points) {
        batch[code] = pt;
        if (batch.size() >= static_cast<size_t>(config_.batchMaxSize_)) {
            if (rtdb_mgr_->batchUpdateYc(batch)) {
                updated += static_cast<int>(batch.size());
                for (const auto& [c, p] : batch) {
                    publish_change("YC", p.to_json().dump());
                    notify_callback("YC", c, true);
                }
            } else {
                for (const auto& [c, _] : batch) {
                    notify_callback("YC", c, false);
                }
            }
            batch.clear();
        }
    }

    // 剩余批次
    if (!batch.empty()) {
        if (rtdb_mgr_->batchUpdateYc(batch)) {
            updated += static_cast<int>(batch.size());
            for (const auto& [c, p] : batch) {
                publish_change("YC", p.to_json().dump());
                notify_callback("YC", c, true);
            }
        } else {
            for (const auto& [c, _] : batch) {
                notify_callback("YC", c, false);
            }
        }
    }

    {
        std::lock_guard lock(stats_mutex_);
        stats_.totalUpdates += updated;
        stats_.batchCount++;
    }

    return updated;
}

int DataDispatcher::batch_update_yx(const std::map<std::string, model::YxPoint>& points) {
    if (!initialized_.load() || !rtdb_mgr_ || points.empty()) return 0;

    int updated = 0;
    std::map<std::string, model::YxPoint> batch;
    for (const auto& [code, pt] : points) {
        batch[code] = pt;
        if (batch.size() >= static_cast<size_t>(config_.batchMaxSize_)) {
            if (rtdb_mgr_->batchUpdateYx(batch)) {
                updated += static_cast<int>(batch.size());
                for (const auto& [c, p] : batch) {
                    publish_change("YX", p.to_json().dump());
                    notify_callback("YX", c, true);
                }
            } else {
                for (const auto& [c, _] : batch) notify_callback("YX", c, false);
            }
            batch.clear();
        }
    }

    if (!batch.empty()) {
        if (rtdb_mgr_->batchUpdateYx(batch)) {
            updated += static_cast<int>(batch.size());
            for (const auto& [c, p] : batch) {
                publish_change("YX", p.to_json().dump());
                notify_callback("YX", c, true);
            }
        } else {
            for (const auto& [c, _] : batch) notify_callback("YX", c, false);
        }
    }

    {
        std::lock_guard lock(stats_mutex_);
        stats_.totalUpdates += updated;
        stats_.batchCount++;
    }

    return updated;
}

int DataDispatcher::batch_update_yk(const std::map<std::string, model::YkPoint>& points) {
    if (!initialized_.load() || !rtdb_mgr_ || points.empty()) return 0;

    if (!rtdb_mgr_->batchUpdateYk(points)) return enum_value(protocol::OperationResult::Failure);
    for (const auto& [code, point] : points) {
        publish_change("YK", point.to_json().dump());
        notify_callback("YK", code, true);
    }
    return static_cast<int>(points.size());
}

int DataDispatcher::batch_update_yt(const std::map<std::string, model::YtPoint>& points) {
    if (!initialized_.load() || !rtdb_mgr_ || points.empty()) return 0;

    if (!rtdb_mgr_->batchUpdateYt(points)) return enum_value(protocol::OperationResult::Failure);
    for (const auto& [code, point] : points) {
        publish_change("YT", point.to_json().dump());
        notify_callback("YT", code, true);
    }
    return static_cast<int>(points.size());
}

// ============================================================
// Pub/Sub 通知
// ============================================================

bool DataDispatcher::publish_change(const std::string& pointType, const std::string& jsonData) {
    if (!initialized_.load() || !rtdb_mgr_) {
        LOG_ERROR("数据分发器未初始化, 无法发布变更");
        return false;
    }

    // 选择对应的发布通道
    std::string channel;
    if (pointType == "YC")
        channel = config_.pubsubChannelYc_;
    else if (pointType == "YX")
        channel = config_.pubsubChannelYx_;
    else if (pointType == "YK")
        channel = config_.pubsubChannelYk_;
    else if (pointType == "YT")
        channel = config_.pubsubChannelYt_;
    else {
        LOG_WARN(fmt::format("未知的测点类型: {}", pointType).c_str());
        return false;
    }

    bool ok = false;
    if (config_.notificationBackend_ == DataDispatcherConfig::NotificationBackend::ZeroMQ) {
#ifdef EMS_WITH_ZEROMQ
        ok = zmq_publisher_ && zmq_publisher_->publish(config_.keyPrefix_ + channel, jsonData);
#endif
    } else {
        auto rtdb = rtdb_mgr_->getRtDb();
        ok = rtdb && rtdb->publish(channel, jsonData);
    }

    {
        std::lock_guard lock(stats_mutex_);
        stats_.totalPublishes++;
        if (!ok) stats_.failedPublishes++;
    }

    if (!ok) {
        LOG_ERROR(fmt::format("发布变更失败: type={}, channel={}", pointType, channel).c_str());
    }

    return ok;
}

// ============================================================
// 统计信息
// ============================================================

DataDispatcher::Stats DataDispatcher::stats() const {
    std::lock_guard lock(stats_mutex_);
    return stats_;
}

// ============================================================
// 私有辅助方法
// ============================================================

model::YcPoint DataDispatcher::make_yc_point(const std::string& code, double value, int quality,
                                             uint64_t timestamp) {
    model::YcPoint pt;
    pt.code_ = code;
    pt.name_ = code;
    pt.value_ = value;
    pt.rawValue_ = static_cast<int64_t>(value);
    pt.timestamp_ = timestamp ? timestamp : now_ms();
    pt.quality_ = static_cast<model::PointQuality>(quality);
    pt.decodeCode_ = model::DecodeCode::FLOAT;
    return pt;
}

model::YxPoint DataDispatcher::make_yx_point(const std::string& code, int value, int quality,
                                             uint64_t timestamp) {
    model::YxPoint pt;
    pt.code_ = code;
    pt.name_ = code;
    pt.value_ = value;
    pt.timestamp_ = timestamp ? timestamp : now_ms();
    pt.quality_ = static_cast<model::PointQuality>(quality);
    pt.decodeCode_ = model::DecodeCode::BOOL;
    return pt;
}

void DataDispatcher::notify_callback(const std::string& type, const std::string& code, bool ok) {
    if (callback_) {
        callback_(type, code, ok);
    }
}

}  // namespace collect
}  // namespace net
}  // namespace ems
