#ifndef EMS_RTDB_CACHE_H
#define EMS_RTDB_CACHE_H

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "rtdb_point.h"

namespace ems {
namespace rtdb {

class RtDbConfigCache {
   public:
    /** @brief 构造 RtDbConfigCache 实例。 */
    RtDbConfigCache() = default;
    /** @brief 释放 RtDbConfigCache 持有的资源。 */
    ~RtDbConfigCache() = default;

    struct YcConfig {
        std::string code;
        std::string name;
        int64_t channel_id{0};
        double mulCoe{1.0};
        double addCoe{0.0};
        double maxLimit{9999999.0};
        double minLimit{-9999999.0};
        std::string unit;
        int status{0};
    };

    struct YxConfig {
        std::string code;
        std::string name;
        int64_t channel_id{0};
        int bit{0};
        bool reverse{false};
        int status{0};
    };

    struct YkConfig {
        std::string code;
        std::string name;
        int64_t channel_id{0};
        int bit{0};
        int commandType{0};
        int64_t relatedYxId{0};
        int status{0};
    };

    struct YtConfig {
        std::string code;
        std::string name;
        int64_t channel_id{0};
        double mulCoe{1.0};
        double addCoe{0.0};
        double maxLimit{9999999.0};
        double minLimit{-9999999.0};
        std::string unit;
        int status{0};
    };

    /**
     * @brief 加载遥测配置。
     * @param[in] configs 配置列表。
     */
    void loadYcConfigs(const std::vector<YcConfig>& configs);
    /**
     * @brief 加载遥信配置。
     * @param[in] configs 配置列表。
     */
    void loadYxConfigs(const std::vector<YxConfig>& configs);
    /**
     * @brief 加载遥控配置。
     * @param[in] configs 配置列表。
     */
    void loadYkConfigs(const std::vector<YkConfig>& configs);
    /**
     * @brief 加载遥调配置。
     * @param[in] configs 配置列表。
     */
    void loadYtConfigs(const std::vector<YtConfig>& configs);

    /**
     * @brief 读取遥测配置。
     * @param[in] code 对象编码。
     * @param[out] config 配置数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getYcConfig(const std::string& code, YcConfig& config) const;
    /**
     * @brief 读取遥信配置。
     * @param[in] code 对象编码。
     * @param[out] config 配置数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getYxConfig(const std::string& code, YxConfig& config) const;
    /**
     * @brief 读取遥控配置。
     * @param[in] code 对象编码。
     * @param[out] config 配置数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getYkConfig(const std::string& code, YkConfig& config) const;
    /**
     * @brief 读取遥调配置。
     * @param[in] code 对象编码。
     * @param[out] config 配置数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getYtConfig(const std::string& code, YtConfig& config) const;

    /**
     * @brief 读取遥测配置编码。
     * @return 匹配条件的数据集合。
     */
    std::vector<std::string> getYcConfigCodes() const;
    /**
     * @brief 读取遥信配置编码。
     * @return 匹配条件的数据集合。
     */
    std::vector<std::string> getYxConfigCodes() const;
    /**
     * @brief 读取遥控配置编码。
     * @return 匹配条件的数据集合。
     */
    std::vector<std::string> getYkConfigCodes() const;
    /**
     * @brief 读取遥调配置编码。
     * @return 匹配条件的数据集合。
     */
    std::vector<std::string> getYtConfigCodes() const;

    /**
     * @brief 读取遥测配置数量。
     * @return 处理或统计得到的数据项数量。
     */
    size_t getYcConfigCount() const;
    /**
     * @brief 读取遥信配置数量。
     * @return 处理或统计得到的数据项数量。
     */
    size_t getYxConfigCount() const;
    /**
     * @brief 读取遥控配置数量。
     * @return 处理或统计得到的数据项数量。
     */
    size_t getYkConfigCount() const;
    /**
     * @brief 读取遥调配置数量。
     * @return 处理或统计得到的数据项数量。
     */
    size_t getYtConfigCount() const;

    /** @brief 清空缓存中的所有配置。 */
    void clear();
    /** @brief 清空遥测测点数据。 */
    void clearYc();
    /** @brief 清空遥信测点数据。 */
    void clearYx();
    /** @brief 清空遥控测点数据。 */
    void clearYk();
    /** @brief 清空遥调测点数据。 */
    void clearYt();

    /**
     * @brief 检查是否存在遥测配置。
     * @param[in] code 对象编码。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool hasYcConfig(const std::string& code) const;
    /**
     * @brief 检查是否存在遥信配置。
     * @param[in] code 对象编码。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool hasYxConfig(const std::string& code) const;
    /**
     * @brief 检查是否存在遥控配置。
     * @param[in] code 对象编码。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool hasYkConfig(const std::string& code) const;
    /**
     * @brief 检查是否存在遥调配置。
     * @param[in] code 对象编码。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool hasYtConfig(const std::string& code) const;

    /**
     * @brief 读取指定测点配置。
     * @param[in] code 对象编码。
     * @param[out] config 配置数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getPointConfig(const std::string& code, PointConfig& config) const;
    /**
     * @brief 写入测点配置。
     * @param[in] config 配置数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool setPointConfig(const PointConfig& config);
    /**
     * @brief 删除指定测点配置。
     * @param[in] code 对象编码。
     * @return true 表示操作成功；false 表示失败。
     */
    bool deletePointConfig(const std::string& code);
    /**
     * @brief 获取缓存中的全部测点配置。
     * @return 匹配条件的数据集合。
     */
    std::vector<PointConfig> getAllPointConfigs() const;

    using UpdateCallback = std::function<void(const std::string& code)>;
    /**
     * @brief 设置配置更新通知回调。
     * @param[in] callback 操作完成或变化时调用的回调。
     */
    void setUpdateCallback(UpdateCallback callback);

   private:
    /**
     * @brief 转换为遥测配置。
     * @param[in] pc 通用测点配置。
     * @return 得到的遥测配置。
     */
    YcConfig toYcConfig(const PointConfig& pc) const;
    /**
     * @brief 转换为遥信配置。
     * @param[in] pc 通用测点配置。
     * @return 得到的遥信配置。
     */
    YxConfig toYxConfig(const PointConfig& pc) const;
    /**
     * @brief 转换为遥控配置。
     * @param[in] pc 通用测点配置。
     * @return 得到的遥控配置。
     */
    YkConfig toYkConfig(const PointConfig& pc) const;
    /**
     * @brief 转换为遥调配置。
     * @param[in] pc 通用测点配置。
     * @return 得到的遥调配置。
     */
    YtConfig toYtConfig(const PointConfig& pc) const;
    /**
     * @brief 转换为通用测点配置。
     * @param[in] yc 遥测配置。
     * @return 得到的测点配置。
     */
    PointConfig toPointConfig(const YcConfig& yc) const;
    /**
     * @brief 转换为通用测点配置。
     * @param[in] yx 遥信配置。
     * @return 得到的测点配置。
     */
    PointConfig toPointConfig(const YxConfig& yx) const;
    /**
     * @brief 转换为通用测点配置。
     * @param[in] yk 遥控配置。
     * @return 得到的测点配置。
     */
    PointConfig toPointConfig(const YkConfig& yk) const;
    /**
     * @brief 转换为通用测点配置。
     * @param[in] yt 遥调配置。
     * @return 得到的测点配置。
     */
    PointConfig toPointConfig(const YtConfig& yt) const;

    mutable std::shared_mutex mutex_;

    std::unordered_map<std::string, YcConfig> ycConfigs_;
    std::unordered_map<std::string, YxConfig> yxConfigs_;
    std::unordered_map<std::string, YkConfig> ykConfigs_;
    std::unordered_map<std::string, YtConfig> ytConfigs_;
    std::unordered_map<std::string, ChannelConfig> channelConfigs_;

    UpdateCallback updateCallback_;
};

class RtDbPointManager {
   public:
    /** @brief 构造 RtDbPointManager 实例。 */
    RtDbPointManager() = default;
    /** @brief 释放 RtDbPointManager 持有的资源。 */
    ~RtDbPointManager() = default;

    struct YcFullPoint {
        RtDbConfigCache::YcConfig config;
        YcPoint data;
    };

    struct YxFullPoint {
        RtDbConfigCache::YxConfig config;
        YxPoint data;
    };

    struct YkFullPoint {
        RtDbConfigCache::YkConfig config;
        YkPoint data;
    };

    struct YtFullPoint {
        RtDbConfigCache::YtConfig config;
        YtPoint data;
    };

    /**
     * @brief 设置测点配置缓存。
     * @param[in] cache 配置缓存实例。
     */
    void setConfigCache(std::shared_ptr<RtDbConfigCache> cache);

    /**
     * @brief 读取遥测完整测点。
     * @param[in] code 对象编码。
     * @param[in] data 测点数据。
     * @param[out] fullPoint 组合后的完整测点。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getYcFullPoint(const std::string& code, const YcPoint& data, YcFullPoint& fullPoint) const;
    /**
     * @brief 读取遥信完整测点。
     * @param[in] code 对象编码。
     * @param[in] data 测点数据。
     * @param[out] fullPoint 组合后的完整测点。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getYxFullPoint(const std::string& code, const YxPoint& data, YxFullPoint& fullPoint) const;
    /**
     * @brief 读取遥控完整测点。
     * @param[in] code 对象编码。
     * @param[in] data 测点数据。
     * @param[out] fullPoint 组合后的完整测点。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getYkFullPoint(const std::string& code, const YkPoint& data, YkFullPoint& fullPoint) const;
    /**
     * @brief 读取遥调完整测点。
     * @param[in] code 对象编码。
     * @param[in] data 测点数据。
     * @param[out] fullPoint 组合后的完整测点。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getYtFullPoint(const std::string& code, const YtPoint& data, YtFullPoint& fullPoint) const;

    /**
     * @brief 读取遥测完整测点。
     * @param[in] dataPoints 待合并配置的运行时测点列表。
     * @return 匹配条件的数据集合。
     */
    std::vector<YcFullPoint> getYcFullPoints(const std::vector<YcPoint>& dataPoints);
    /**
     * @brief 读取遥信完整测点。
     * @param[in] dataPoints 待合并配置的运行时测点列表。
     * @return 匹配条件的数据集合。
     */
    std::vector<YxFullPoint> getYxFullPoints(const std::vector<YxPoint>& dataPoints);
    /**
     * @brief 读取遥控完整测点。
     * @param[in] dataPoints 待合并配置的运行时测点列表。
     * @return 匹配条件的数据集合。
     */
    std::vector<YkFullPoint> getYkFullPoints(const std::vector<YkPoint>& dataPoints);
    /**
     * @brief 读取遥调完整测点。
     * @param[in] dataPoints 待合并配置的运行时测点列表。
     * @return 匹配条件的数据集合。
     */
    std::vector<YtFullPoint> getYtFullPoints(const std::vector<YtPoint>& dataPoints);

    /**
     * @brief 按遥测配置转换原始数值。
     * @param[in] rawValue 原始采集值。
     * @param[in] config 配置数据。
     * @return 按指定类型解释的数值。
     */
    double transformYcValue(double rawValue, const RtDbConfigCache::YcConfig& config) const;
    /**
     * @brief 按遥调配置转换原始数值。
     * @param[in] rawValue 原始采集值。
     * @param[in] config 配置数据。
     * @return 按指定类型解释的数值。
     */
    double transformYtValue(double rawValue, const RtDbConfigCache::YtConfig& config) const;

    /**
     * @brief 校验遥测值是否符合配置约束。
     * @param[in] value 输入值。
     * @param[in] config 配置数据。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool validateYcValue(double value, const RtDbConfigCache::YcConfig& config) const;
    /**
     * @brief 校验遥调值是否符合配置约束。
     * @param[in] value 输入值。
     * @param[in] config 配置数据。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool validateYtValue(double value, const RtDbConfigCache::YtConfig& config) const;

   private:
    std::shared_ptr<RtDbConfigCache> configCache_;
};

}  // namespace rtdb
}  // namespace ems

#endif