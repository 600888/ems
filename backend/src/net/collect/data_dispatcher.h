#pragma once
#include <rtdb/client/point_client.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

#include "net/collect/collect_types.h"
#include "net/handler/protocol_handler.h"

namespace ems::net::collect {
using handler::PointValue;

struct DataDispatcherConfig {
    std::string rtdbConfigPath_{"conf/realtime.json"};
    std::string rtdbScopeCode_{"station001"};
    int timeoutMs_{3000};
};

using DispatchCallback = std::function<void(const std::string&, const std::string&, bool)>;

/** @brief 采集和反馈共用的类型化实时库写入端，不维护独立缓存或旧键格式。 */
class DataDispatcher {
   public:
    /** @brief 创建尚未配置的分发器。 */
    DataDispatcher();
    /** @brief 释放实时库客户端。 */
    ~DataDispatcher();
    /**
     * @brief 验证部署后端可挂接，保存连接配置。
     * @param[in] config 部署配置。
     * @return 初始化是否成功；不创建共享段。
     */
    bool initialize(const DataDispatcherConfig& config);
    /**
     * @brief 在通道停止期间安装完整目录和唯一写来源。
     * @param[in] catalog 当前站根修订号对应目录。
     * @return 全部安装成功为 true；失败不替换当前客户端。
     */
    bool configure(std::shared_ptr<const rtdb::RuntimeCatalog> catalog);
    /** @brief 关闭本进程客户端；调用方须先停止全部通道。 */
    void shutdown();
    /** @brief 检查部署初始化状态。 @return 是否已初始化。 */
    bool is_initialized() const;
    /** @brief 返回当前点客户端。 @return 共享客户端；尚未安装目录时为空。 */
    std::shared_ptr<rtdb::PointClient> point_client() const;
    /** @brief 检查目录是否已安装。 @return 是否有可用客户端。 */
    bool is_connected() const;
    /**
     * @brief 批量发布采集或反馈样本，按稳定 ID 查找类型化目录。
     * @param[in] results 已完成工程量换算的样本。
     * @return 成功更新的点数；没有客户端时为 -1。
     */
    int batch_update(const std::vector<PointValue>& results);
    /**
     * @brief 写入遥测工程值。
     * @param[in] id 稳定点 ID。
     * @param[in] value 工程值。
     * @param[in] quality 插件品质码。
     * @param[in] timestamp 源 UTC 毫秒时间。
     * @return 是否成功。
     */
    bool update_yc(const std::string& id, double value, int quality, uint64_t timestamp);
    /**
     * @brief 写入遥信工程值。
     * @param[in] id 稳定点 ID。
     * @param[in] value 工程值。
     * @param[in] quality 插件品质码。
     * @param[in] timestamp 源 UTC 毫秒时间。
     * @return 是否成功。
     */
    bool update_yx(const std::string& id, int value, int quality, uint64_t timestamp);
    /**
     * @brief 注册结果回调；须在启动通道前设置。
     * @param[in] callback 不执行设备控制的结果回调。
     */
    void set_dispatch_callback(DispatchCallback callback);

    struct Stats {
        uint64_t totalUpdates{0}, totalPublishes{0}, failedUpdates{0}, failedPublishes{0},
            batchCount{0};
    };

    /** @brief 读取统计副本。 @return 当前累计统计。 */
    Stats stats() const;

   private:
    mutable std::mutex mutex_;
    DataDispatcherConfig config_;
    std::shared_ptr<rtdb::PointClient> client_;
    DispatchCallback callback_;
    bool initialized_{false};
    Stats stats_;
};
}  // namespace ems::net::collect
