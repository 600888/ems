#ifndef EMS_RTDB_MANAGER_H
#define EMS_RTDB_MANAGER_H

#include <functional>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "rtdb/client/rtdb.h"
#include "rtdb/model/rtdb_cache.h"

namespace ems {
namespace rtdb {

class EMS_RTDB_API RtDbManager {
   public:
    /** @brief 构造 RtDbManager 实例。 */
    RtDbManager();
    /** @brief 释放 RtDbManager 持有的资源。 */
    ~RtDbManager();

    /**
     * @brief 初始化连接和运行配置。
     * @param[in] config 配置数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool initialize(const RtDbConfig& config);
    /** @brief 停止服务并释放资源。 */
    void shutdown();

    /**
     * @brief 获取底层实时数据库实例。
     * @return 匹配的对象指针；未找到时为空。
     */
    std::shared_ptr<RtDb> getRtDb() const { return rtdb_; }

    /**
     * @brief 获取测点配置缓存。
     * @return 匹配的对象指针；未找到时为空。
     */
    std::shared_ptr<RtDbConfigCache> getConfigCache() const { return configCache_; }

    /**
     * @brief 获取测点管理器。
     * @return 匹配的对象指针；未找到时为空。
     */
    std::shared_ptr<RtDbPointManager> getPointManager() const { return pointManager_; }

    /**
     * @brief 从数据库加载遥测配置。
     * @return true 表示操作成功；false 表示失败。
     */
    bool loadYcConfigsFromDb();
    /**
     * @brief 从数据库加载遥信配置。
     * @return true 表示操作成功；false 表示失败。
     */
    bool loadYxConfigsFromDb();
    /**
     * @brief 从数据库加载遥控配置。
     * @return true 表示操作成功；false 表示失败。
     */
    bool loadYkConfigsFromDb();
    /**
     * @brief 从数据库加载遥调配置。
     * @return true 表示操作成功；false 表示失败。
     */
    bool loadYtConfigsFromDb();
    /**
     * @brief 从数据库加载全部测点配置。
     * @return true 表示操作成功；false 表示失败。
     */
    bool loadAllConfigs();

    using DataChangeCallback = std::function<void(const std::string& pointType,
                                                  const std::string& pointCode, const void* data)>;

    /**
     * @brief 设置数据变化通知回调。
     * @param[in] callback 操作完成或变化时调用的回调。
     */
    void setDataChangeCallback(DataChangeCallback callback);

    /**
     * @brief 更新遥测测点数据。
     * @param[in] code 对象编码。
     * @param[in] data 测点数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool updateYc(const std::string& code, const YcPoint& data);
    /**
     * @brief 更新遥信测点数据。
     * @param[in] code 对象编码。
     * @param[in] data 测点数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool updateYx(const std::string& code, const YxPoint& data);
    /**
     * @brief 更新遥控测点数据。
     * @param[in] code 对象编码。
     * @param[in] data 测点数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool updateYk(const std::string& code, const YkPoint& data);
    /**
     * @brief 更新遥调测点数据。
     * @param[in] code 对象编码。
     * @param[in] data 测点数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool updateYt(const std::string& code, const YtPoint& data);

    /**
     * @brief 批量更新遥测测点数据。
     * @param[in] dataMap 待写入的测点映射。
     * @return true 表示操作成功；false 表示失败。
     */
    bool batchUpdateYc(const std::map<std::string, YcPoint>& dataMap);
    /**
     * @brief 批量更新遥信测点数据。
     * @param[in] dataMap 待写入的测点映射。
     * @return true 表示操作成功；false 表示失败。
     */
    bool batchUpdateYx(const std::map<std::string, YxPoint>& dataMap);
    /**
     * @brief 批量更新遥控测点数据。
     * @param[in] dataMap 待写入的测点映射。
     * @return true 表示操作成功；false 表示失败。
     */
    bool batchUpdateYk(const std::map<std::string, YkPoint>& dataMap);
    /**
     * @brief 批量更新遥调测点数据。
     * @param[in] dataMap 待写入的测点映射。
     * @return true 表示操作成功；false 表示失败。
     */
    bool batchUpdateYt(const std::map<std::string, YtPoint>& dataMap);

    /**
     * @brief 异步更新遥测测点数据。
     * @param[in] code 对象编码。
     * @param[in] data 测点数据。
     * @return 用于获取异步操作结果的 future。
     */
    std::future<bool> asyncUpdateYc(const std::string& code, const YcPoint& data);
    /**
     * @brief 异步更新遥信测点数据。
     * @param[in] code 对象编码。
     * @param[in] data 测点数据。
     * @return 用于获取异步操作结果的 future。
     */
    std::future<bool> asyncUpdateYx(const std::string& code, const YxPoint& data);
    /**
     * @brief 异步更新遥控测点数据。
     * @param[in] code 对象编码。
     * @param[in] data 测点数据。
     * @return 用于获取异步操作结果的 future。
     */
    std::future<bool> asyncUpdateYk(const std::string& code, const YkPoint& data);
    /**
     * @brief 异步更新遥调测点数据。
     * @param[in] code 对象编码。
     * @param[in] data 测点数据。
     * @return 用于获取异步操作结果的 future。
     */
    std::future<bool> asyncUpdateYt(const std::string& code, const YtPoint& data);

    /**
     * @brief 读取遥测测点数据。
     * @param[in] code 对象编码。
     * @param[out] data 测点数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getYc(const std::string& code, YcPoint& data);
    /**
     * @brief 读取遥信测点数据。
     * @param[in] code 对象编码。
     * @param[out] data 测点数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getYx(const std::string& code, YxPoint& data);
    /**
     * @brief 读取遥控测点数据。
     * @param[in] code 对象编码。
     * @param[out] data 测点数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getYk(const std::string& code, YkPoint& data);
    /**
     * @brief 读取遥调测点数据。
     * @param[in] code 对象编码。
     * @param[out] data 测点数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getYt(const std::string& code, YtPoint& data);

    /**
     * @brief 读取遥测完整测点。
     * @param[in] code 对象编码。
     * @param[out] fullPoint 组合后的完整测点。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getYcFull(const std::string& code, RtDbPointManager::YcFullPoint& fullPoint);
    /**
     * @brief 读取遥信完整测点。
     * @param[in] code 对象编码。
     * @param[out] fullPoint 组合后的完整测点。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getYxFull(const std::string& code, RtDbPointManager::YxFullPoint& fullPoint);
    /**
     * @brief 读取遥控完整测点。
     * @param[in] code 对象编码。
     * @param[out] fullPoint 组合后的完整测点。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getYkFull(const std::string& code, RtDbPointManager::YkFullPoint& fullPoint);
    /**
     * @brief 读取遥调完整测点。
     * @param[in] code 对象编码。
     * @param[out] fullPoint 组合后的完整测点。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getYtFull(const std::string& code, RtDbPointManager::YtFullPoint& fullPoint);

    /**
     * @brief 获取全部遥测运行数据。
     * @return 匹配条件的数据集合。
     */
    std::vector<YcPoint> getAllYcData();
    /**
     * @brief 获取全部遥信运行数据。
     * @return 匹配条件的数据集合。
     */
    std::vector<YxPoint> getAllYxData();
    /**
     * @brief 获取全部遥控运行数据。
     * @return 匹配条件的数据集合。
     */
    std::vector<YkPoint> getAllYkData();
    /**
     * @brief 获取全部遥调运行数据。
     * @return 匹配条件的数据集合。
     */
    std::vector<YtPoint> getAllYtData();

    /**
     * @brief 获取全部遥测完整测点。
     * @return 匹配条件的数据集合。
     */
    std::vector<RtDbPointManager::YcFullPoint> getAllYcFullPoints();
    /**
     * @brief 获取全部遥信完整测点。
     * @return 匹配条件的数据集合。
     */
    std::vector<RtDbPointManager::YxFullPoint> getAllYxFullPoints();
    /**
     * @brief 获取全部遥控完整测点。
     * @return 匹配条件的数据集合。
     */
    std::vector<RtDbPointManager::YkFullPoint> getAllYkFullPoints();
    /**
     * @brief 获取全部遥调完整测点。
     * @return 匹配条件的数据集合。
     */
    std::vector<RtDbPointManager::YtFullPoint> getAllYtFullPoints();

    /**
     * @brief 读取遥测运行数据数量。
     * @return 处理或统计得到的数据项数量。
     */
    size_t getYcDataCount();
    /**
     * @brief 读取遥信运行数据数量。
     * @return 处理或统计得到的数据项数量。
     */
    size_t getYxDataCount();
    /**
     * @brief 读取遥控运行数据数量。
     * @return 处理或统计得到的数据项数量。
     */
    size_t getYkDataCount();
    /**
     * @brief 读取遥调运行数据数量。
     * @return 处理或统计得到的数据项数量。
     */
    size_t getYtDataCount();

    /**
     * @brief 检查连接是否可用。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool isConnected() const { return rtdb_ && rtdb_->isConnected(); }

    /**
     * @brief 获取最近一次错误代码或说明。
     * @return 最近一次错误的说明文本。
     */
    std::string getLastError() const {
        return rtdb_ ? rtdb_->getLastError() : "RTDB not initialized";
    }

    /** @brief 清空 RTDB 管理器维护的所有运行数据。 */
    void clearAllData();

    /**
     * @brief 从 JSON 文件加载测点配置。
     * @param[in] filepath 配置文件路径。
     * @return true 表示操作成功；false 表示失败。
     */
    bool loadConfigFromJson(const std::string& filepath);
    /**
     * @brief 从 CSV 文件加载测点配置。
     * @param[in] filepath 配置文件路径。
     * @return true 表示操作成功；false 表示失败。
     */
    bool loadConfigFromCsv(const std::string& filepath);

   private:
    std::shared_ptr<RtDb> rtdb_;
    std::shared_ptr<RtDbConfigCache> configCache_;
    std::shared_ptr<RtDbPointManager> pointManager_;
    DataChangeCallback dataChangeCallback_;
};

/**
 * @brief 创建并初始化 RTDB 管理器。
 * @param[in] config 配置数据。
 * @return 创建的对象；创建失败时为空。
 */
std::unique_ptr<RtDbManager> createRtdbManager(const RtDbConfig& config);

}  // namespace rtdb
}  // namespace ems

#endif