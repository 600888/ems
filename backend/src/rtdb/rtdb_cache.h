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

class RTDBConfigCache {
public:
    RTDBConfigCache() = default;
    ~RTDBConfigCache() = default;

    struct YCConfig {
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

    struct YXConfig {
        std::string code;
        std::string name;
        int64_t channel_id{0};
        int bit{0};
        bool reverse{false};
        int status{0};
    };

    struct YKConfig {
        std::string code;
        std::string name;
        int64_t channel_id{0};
        int bit{0};
        int command_type{0};
        int64_t related_yx_id{0};
        int status{0};
    };

    struct YTConfig {
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

    void loadYCConfigs(const std::vector<YCConfig>& configs);
    void loadYXConfigs(const std::vector<YXConfig>& configs);
    void loadYKConfigs(const std::vector<YKConfig>& configs);
    void loadYTConfigs(const std::vector<YTConfig>& configs);

    bool getYCConfig(const std::string& code, YCConfig& config) const;
    bool getYXConfig(const std::string& code, YXConfig& config) const;
    bool getYKConfig(const std::string& code, YKConfig& config) const;
    bool getYTConfig(const std::string& code, YTConfig& config) const;

    std::vector<std::string> getYCConfigCodes() const;
    std::vector<std::string> getYXConfigCodes() const;
    std::vector<std::string> getYKConfigCodes() const;
    std::vector<std::string> getYTConfigCodes() const;

    size_t getYCConfigCount() const;
    size_t getYXConfigCount() const;
    size_t getYKConfigCount() const;
    size_t getYTConfigCount() const;

    void clear();
    void clearYC();
    void clearYX();
    void clearYK();
    void clearYT();

    bool hasYCConfig(const std::string& code) const;
    bool hasYXConfig(const std::string& code) const;
    bool hasYKConfig(const std::string& code) const;
    bool hasYTConfig(const std::string& code) const;

    using UpdateCallback = std::function<void(const std::string& code)>;
    void setUpdateCallback(UpdateCallback callback);

private:
    mutable std::shared_mutex mutex_;

    std::unordered_map<std::string, YCConfig> yc_configs_;
    std::unordered_map<std::string, YXConfig> yx_configs_;
    std::unordered_map<std::string, YKConfig> yk_configs_;
    std::unordered_map<std::string, YTConfig> yt_configs_;

    UpdateCallback update_callback_;
};

class RTDBPointManager {
public:
    RTDBPointManager() = default;
    ~RTDBPointManager() = default;

    struct YCFullPoint {
        RTDBConfigCache::YCConfig config;
        YCPoint data;
    };

    struct YXFullPoint {
        RTDBConfigCache::YXConfig config;
        YXPoint data;
    };

    struct YKFullPoint {
        RTDBConfigCache::YKConfig config;
        YKPoint data;
    };

    struct YTFullPoint {
        RTDBConfigCache::YTConfig config;
        YTPoint data;
    };

    void setConfigCache(std::shared_ptr<RTDBConfigCache> cache);

    bool getYCFullPoint(const std::string& code, 
                       const YCPoint& data, 
                       YCFullPoint& full_point) const;
    bool getYXFullPoint(const std::string& code, 
                       const YXPoint& data, 
                       YXFullPoint& full_point) const;
    bool getYKFullPoint(const std::string& code, 
                       const YKPoint& data, 
                       YKFullPoint& full_point) const;
    bool getYTFullPoint(const std::string& code, 
                       const YTPoint& data, 
                       YTFullPoint& full_point) const;

    std::vector<YCFullPoint> getYCFullPoints(const std::vector<YCPoint>& data_points);
    std::vector<YXFullPoint> getYXFullPoints(const std::vector<YXPoint>& data_points);
    std::vector<YKFullPoint> getYKFullPoints(const std::vector<YKPoint>& data_points);
    std::vector<YTFullPoint> getYTFullPoints(const std::vector<YTPoint>& data_points);

    double transformYCValue(double raw_value, const RTDBConfigCache::YCConfig& config) const;
    double transformYTValue(double raw_value, const RTDBConfigCache::YTConfig& config) const;

    bool validateYCValue(double value, const RTDBConfigCache::YCConfig& config) const;
    bool validateYTValue(double value, const RTDBConfigCache::YTConfig& config) const;

private:
    std::shared_ptr<RTDBConfigCache> config_cache_;
};

}
}

#endif
