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
namespace net {

class RtDbConfigCache {
public:
    RtDbConfigCache() = default;
    ~RtDbConfigCache() = default;

    struct YcConfig {
        std::string code;
        std::string name;
        int64_t channel_id{0};
        double mul_coe{1.0};
        double add_coe{0.0};
        double max_limit{9999999.0};
        double min_limit{-9999999.0};
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
        int command_type{0};
        int64_t related_yx_id{0};
        int status{0};
    };

    struct YtConfig {
        std::string code;
        std::string name;
        int64_t channel_id{0};
        double mul_coe{1.0};
        double add_coe{0.0};
        double max_limit{9999999.0};
        double min_limit{-9999999.0};
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

    using UpdateCallback = std::function<void(const std::string& code)>;
    void setUpdateCallback(UpdateCallback callback);

private:
    mutable std::shared_mutex mutex_;

    std::unordered_map<std::string, YcConfig> yc_configs_;
    std::unordered_map<std::string, YxConfig> yx_configs_;
    std::unordered_map<std::string, YkConfig> yk_configs_;
    std::unordered_map<std::string, YtConfig> yt_configs_;

    UpdateCallback update_callback_;
};

class RtDbPointManager {
public:
    RtDbPointManager() = default;
    ~RtDbPointManager() = default;

    struct YCFullPoint {
        RtDbConfigCache::YcConfig config;
        YcPoint data;
    };

    struct YXFullPoint {
        RtDbConfigCache::YxConfig config;
        YxPoint data;
    };

    struct YKFullPoint {
        RtDbConfigCache::YkConfig config;
        YkPoint data;
    };

    struct YTFullPoint {
        RtDbConfigCache::YtConfig config;
        YtPoint data;
    };

    void setConfigCache(std::shared_ptr<RtDbConfigCache> cache);

    bool getYCFullPoint(const std::string& code, 
                       const YcPoint& data, 
                       YCFullPoint& full_point) const;
    bool getYXFullPoint(const std::string& code, 
                       const YxPoint& data, 
                       YXFullPoint& full_point) const;
    bool getYKFullPoint(const std::string& code, 
                       const YkPoint& data, 
                       YKFullPoint& full_point) const;
    bool getYTFullPoint(const std::string& code, 
                       const YtPoint& data, 
                       YTFullPoint& full_point) const;

    std::vector<YCFullPoint> getYCFullPoints(const std::vector<YcPoint>& data_points);
    std::vector<YXFullPoint> getYXFullPoints(const std::vector<YxPoint>& data_points);
    std::vector<YKFullPoint> getYKFullPoints(const std::vector<YkPoint>& data_points);
    std::vector<YTFullPoint> getYTFullPoints(const std::vector<YtPoint>& data_points);

    double transformYCValue(double raw_value, const RtDbConfigCache::YcConfig& config) const;
    double transformYTValue(double raw_value, const RtDbConfigCache::YtConfig& config) const;

    bool validateYCValue(double value, const RtDbConfigCache::YcConfig& config) const;
    bool validateYTValue(double value, const RtDbConfigCache::YtConfig& config) const;

private:
    std::shared_ptr<RtDbConfigCache> config_cache_;
};

}
}

#endif
