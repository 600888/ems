/**
 * @file data_dispatcher.h
 * @brief 采集数据分发器
 *
 * 将采集线程读取到的数据批量写入旧 RTDB (Redis)，并通过可选的
 * Redis 或 ZeroMQ Pub/Sub 通知其他进程。
 * 支持 YC/YX/YK/YT 四类测点的分别写入和批量优化。
 */
#ifndef EMS_NET_COLLECT_DATA_DISPATCHER_H
#define EMS_NET_COLLECT_DATA_DISPATCHER_H

#include <atomic>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "models/point_runtime.hpp"
#include "net/collect/collect_defaults.h"
#include "net/collect/collect_types.h"
#include "net/handler/protocol_handler.h"

namespace ems {
namespace rtdb {
class RtDbManager;
}

namespace messaging {
class ZmqChangePublisher;
}

namespace net {
namespace collect {

using handler::PointValue;

/**
 * @brief 数据分发配置
 */
struct DataDispatcherConfig {
    using NotificationBackend = collect::NotificationBackend;

    std::string redisHost_{"127.0.0.1"};
    int redisPort_{enum_value(DispatchDefaults::RedisPort)};
    std::string redisPassword_;
    int redisDb_{0};
    int poolSize_{enum_value(DispatchDefaults::RedisPoolSize)};
    int timeoutMs_{enum_value(DispatchDefaults::RedisTimeoutMs)};
    std::string keyPrefix_{"ems:"};

    // Pub/Sub 通知通道
    std::string pubsubChannelYc_{"rtdb:yc:update"};
    std::string pubsubChannelYx_{"rtdb:yx:update"};
    std::string pubsubChannelYk_{"rtdb:yk:update"};
    std::string pubsubChannelYt_{"rtdb:yt:update"};
    NotificationBackend notificationBackend_{NotificationBackend::Redis};
    std::string zmqEndpoint_{"tcp://127.0.0.1:5557"};
    std::size_t zmqQueueItems_{enum_value(DispatchDefaults::ZmqQueueItems)};
    std::size_t zmqMaxMessageBytes_{enum_value(DispatchDefaults::ZmqMaxMessageBytes)};
    int zmqHighWaterMark_{enum_value(DispatchDefaults::ZmqHighWaterMark)};

    // 批量写入配置
    int batchMaxSize_{enum_value(DispatchDefaults::BatchMaxSize)};        // 单次批量最大点数
    int batchIntervalMs_{enum_value(DispatchDefaults::BatchIntervalMs)};  // 批量写入最大间隔(ms)
};

/**
 * @brief 分发结果回调
 */
using DispatchCallback = std::function<void(const std::string& pointType,  // "YC"|"YX"|"YK"|"YT"
                                            const std::string& pointCode, bool success)>;

/**
 * @brief 数据分发器
 *
 * 线程安全，可在多个采集线程中并发调用。
 */
class DataDispatcher {
   public:
    /** @brief 构造 DataDispatcher 实例。 */
    DataDispatcher();
    /** @brief 释放 DataDispatcher 持有的资源。 */
    ~DataDispatcher();

    /**
     * @brief 禁止复制或移动该对象。
     * @param[in] other 待复制或移动的源对象。
     */
    DataDispatcher(const DataDispatcher& other) = delete;
    /**
     * @brief 禁止复制赋值。
     * @param[in] other 待赋值的源对象。
     */
    DataDispatcher& operator=(const DataDispatcher& other) = delete;
    /**
     * @brief 禁止复制或移动该对象。
     * @param[in] other 待复制或移动的源对象。
     */
    DataDispatcher(DataDispatcher&& other) noexcept = delete;
    /**
     * @brief 禁止复制或移动赋值。
     * @param[in] other 待赋值的源对象。
     */
    DataDispatcher& operator=(DataDispatcher&& other) noexcept = delete;

    /**
     * @brief 初始化 RTDB 连接
     * @param[in] config 配置参数
     * @return true 成功
     */
    bool initialize(const DataDispatcherConfig& config);

    /**
     * @brief 关闭分发器
     */
    void shutdown();

    /**
     * @brief 是否已初始化
     * @return true 表示条件成立；false 表示不成立。
     */
    bool is_initialized() const { return initialized_.load(); }

    /**
     * @brief 更新单个测点值（YC 遥测）
     * @param[in] pointCode 测点编码
     * @param[in] value     工程值
     * @param[in] quality   品质
     * @param[in] timestamp 时间戳
     * @return true 成功
     */
    bool update_yc(const std::string& pointCode, double value, int quality, uint64_t timestamp);

    /**
     * @brief 更新单个测点值（YX 遥信）
     * @param[in] pointCode 测点编码。
     * @param[in] value 待写入的值。
     * @param[in] quality 测点品质码。
     * @param[in] timestamp 数据时间戳。
     * @return true 表示操作成功；false 表示失败。
     */
    bool update_yx(const std::string& pointCode, int value, int quality, uint64_t timestamp);

    /**
     * @brief 更新单个测点值（YK 遥控状态）
     * @param[in] pointCode 测点编码。
     * @param[in] value 待写入的值。
     * @param[in] quality 测点品质码。
     * @param[in] ykStatus 遥控执行状态。
     * @param[in] timestamp 数据时间戳。
     * @return true 表示操作成功；false 表示失败。
     */
    bool update_yk(const std::string& pointCode, int value, int quality, int ykStatus,
                   uint64_t timestamp);

    /**
     * @brief 更新单个测点值（YT 遥调）
     * @param[in] pointCode 测点编码。
     * @param[in] value 待写入的值。
     * @param[in] quality 测点品质码。
     * @param[in] timestamp 数据时间戳。
     * @return true 表示操作成功；false 表示失败。
     */
    bool update_yt(const std::string& pointCode, double value, int quality, uint64_t timestamp);

    /**
     * @brief 批量更新测点数据（自动区分 YC/YX）
     *
     * 根据 PointValue 中的 pointCode 前缀自动判定测点类型。
     * 内部合并同类测点批量提交至 RTDB。
     *
     * @param[out] results 测点值列表
     * @return 成功更新的测点数
     */
    int batch_update(const std::vector<PointValue>& results);

    /**
     * @brief 批量更新 YC 测点
     * @param[in] points 测点编码 -> YcPoint 映射
     * @return 成功更新的测点数
     */
    int batch_update_yc(const std::map<std::string, model::YcPoint>& points);

    /**
     * @brief 批量更新 YX 测点
     * @param[in] points 测点数据列表。
     * @return 成功处理的数据项数量；失败时返回错误值。
     */
    int batch_update_yx(const std::map<std::string, model::YxPoint>& points);

    /**
     * @brief 批量更新 YK 测点
     * @param[in] points 测点数据列表。
     * @return 成功处理的数据项数量；失败时返回错误值。
     */
    int batch_update_yk(const std::map<std::string, model::YkPoint>& points);

    /**
     * @brief 批量更新 YT 测点
     * @param[in] points 测点数据列表。
     * @return 成功处理的数据项数量；失败时返回错误值。
     */
    int batch_update_yt(const std::map<std::string, model::YtPoint>& points);

    /**
     * @brief 发布变更通知
     * @param[in] pointType 测点类型
     * @param[in] jsonData  变更数据 JSON
     * @return true 表示通知后端接受；不保证订阅者收到。
     */
    bool publish_change(const std::string& pointType, const std::string& jsonData);

    /**
     * @brief 设置分发回调
     * @param[in] cb 操作完成或变化时调用的回调。
     */
    void set_dispatch_callback(DispatchCallback cb) { callback_ = std::move(cb); }

    /**
     * @brief 获取 RTDB 管理器实例（非内联，定义在 cpp）
     * @return 对应对象的指针。
     */
    std::shared_ptr<ems::rtdb::RtDbManager> rtdb_manager() const;

    /**
     * @brief 获取连接状态（非内联，定义在 cpp）
     * @return true 表示条件成立；false 表示不成立。
     */
    bool is_connected() const;

    /**
     * @brief 获取统计信息
     */
    struct Stats {
        uint64_t totalUpdates{0};
        uint64_t totalPublishes{0};
        uint64_t failedUpdates{0};
        uint64_t failedPublishes{0};
        uint64_t batchCount{0};
    };

    /**
     * @brief 获取数据分发统计信息。
     * @return 得到的统计信息。
     */
    Stats stats() const;

   private:
    /**
     * @brief 根据采集值构造遥测运行数据。
     * @param[in] code 对象编码。
     * @param[in] value 输入值。
     * @param[in] quality 测点品质码。
     * @param[in] timestamp 数据时间戳。
     * @return 得到的遥测测点数据。
     */
    model::YcPoint make_yc_point(const std::string& code, double value, int quality,
                                 uint64_t timestamp);
    /**
     * @brief 根据采集值构造遥信运行数据。
     * @param[in] code 对象编码。
     * @param[in] value 输入值。
     * @param[in] quality 测点品质码。
     * @param[in] timestamp 数据时间戳。
     * @return 得到的遥信测点数据。
     */
    model::YxPoint make_yx_point(const std::string& code, int value, int quality,
                                 uint64_t timestamp);

    /**
     * @brief 向调用方报告测点分发结果。
     * @param[in] type 连接或数据类型。
     * @param[in] code 对象编码。
     * @param[in] ok 操作是否成功。
     */
    void notify_callback(const std::string& type, const std::string& code, bool ok);

    std::atomic<bool> initialized_{false};
    std::shared_ptr<ems::rtdb::RtDbManager> rtdb_mgr_;
    std::shared_ptr<ems::messaging::ZmqChangePublisher> zmq_publisher_;
    DataDispatcherConfig config_;
    DispatchCallback callback_;

    // 统计信息
    mutable std::mutex stats_mutex_;
    Stats stats_;
};

}  // namespace collect
}  // namespace net
}  // namespace ems

#endif  // EMS_NET_COLLECT_DATA_DISPATCHER_H
