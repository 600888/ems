#ifndef EMS_RTDB_CACHE_H
#define EMS_RTDB_CACHE_H

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <chrono>
#include <memory>
#include "rtdb_point.h"

namespace ems {
namespace rtdb {

class RtDbConfigCache {
public:
    RtDbConfigCache() = default;
    ~RtDbConfigCache() = default;

    struct YcConfig {
        std::string code;
        std::string name;
        int64_t channelId{0};
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
        int64_t channelId{0};
        int bit{0};
        bool reverse{false};
        int status{0};
    };

    struct YkConfig {
        std::string code;
        std::string name;
        int64_t channelId{0};
        int bit{0};
        int commandType{0};
        int64_t relatedYxId{0};
        int status{0};
    };

    struct YtConfig {
        std::string code;
        std::string name;
        int64_t channelId{0};
        double mulCoe{1.0};
        double addCoe{0.0};
        double maxLimit{9999999.0};
        double minLimit{-9999999.0};
        std::string unit;
        int status{0};
    };

    void loadYcConfigs(const std::vector<YcConfig>& configs);
    void loadYxConfigs(const std::vector<YxConfig>& configs);
    void loadYkConfigs(const std::vector<YkConfig>& configs);
    void loadYtConfigs(const std::vector<YtConfig>& configs);

    bool getYcConfig(const std::string& code, YcConfig& config) const;
    bool getYxConfig(const std::string& code, YxConfig& config) const;
    bool getYkConfig(const std::string& code, YkConfig& config) const;
    bool getYtConfig(const std::string& code, YtConfig& config) const;

    std::vector<std::string> getYcConfigCodes() const;
    std::vector<std::string> getYxConfigCodes() const;
    std::vector<std::string> getYkConfigCodes() const;
    std::vector<std::string> getYtConfigCodes() const;

    size_t getYcConfigCount() const;
    size_t getYxConfigCount() const;
    size_t getYkConfigCount() const;
    size_t getYtConfigCount() const;

    void clear();
    void clearYc();
    void clearYx();
    void clearYk();
    void clearYt();

    bool hasYcConfig(const std::string& code) const;
    bool hasYxConfig(const std::string& code) const;
    bool hasYkConfig(const std::string& code) const;
    bool hasYtConfig(const std::string& code) const;

    bool getPointConfig(const std::string& code, PointConfig& config) const;
    bool setPointConfig(const PointConfig& config);
    bool deletePointConfig(const std::string& code);
    std::vector<PointConfig> getAllPointConfigs() const;

    using UpdateCallback = std::function<void(const std::string& code)>;
    void setUpdateCallback(UpdateCallback callback);

private:
    YcConfig toYcConfig(const PointConfig& pc) const;
    YxConfig toYxConfig(const PointConfig& pc) const;
    YkConfig toYkConfig(const PointConfig& pc) const;
    YtConfig toYtConfig(const PointConfig& pc) const;
    PointConfig toPointConfig(const YcConfig& yc) const;
    PointConfig toPointConfig(const YxConfig& yx) const;
    PointConfig toPointConfig(const YkConfig& yk) const;
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
    RtDbPointManager() = default;
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

    void setConfigCache(std::shared_ptr<RtDbConfigCache> cache);

    bool getYcFullPoint(const std::string& code,
                       const YcPoint& data,
                       YcFullPoint& fullPoint) const;
    bool getYxFullPoint(const std::string& code,
                       const YxPoint& data,
                       YxFullPoint& fullPoint) const;
    bool getYkFullPoint(const std::string& code,
                       const YkPoint& data,
                       YkFullPoint& fullPoint) const;
    bool getYtFullPoint(const std::string& code,
                       const YtPoint& data,
                       YtFullPoint& fullPoint) const;

    std::vector<YcFullPoint> getYcFullPoints(const std::vector<YcPoint>& dataPoints);
    std::vector<YxFullPoint> getYxFullPoints(const std::vector<YxPoint>& dataPoints);
    std::vector<YkFullPoint> getYkFullPoints(const std::vector<YkPoint>& dataPoints);
    std::vector<YtFullPoint> getYtFullPoints(const std::vector<YtPoint>& dataPoints);

    double transformYcValue(double rawValue, const RtDbConfigCache::YcConfig& config) const;
    double transformYtValue(double rawValue, const RtDbConfigCache::YtConfig& config) const;

    bool validateYcValue(double value, const RtDbConfigCache::YcConfig& config) const;
    bool validateYtValue(double value, const RtDbConfigCache::YtConfig& config) const;

private:
    std::shared_ptr<RtDbConfigCache> configCache_;
};

}
}

#endif