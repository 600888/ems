#include "rtdb/rtdb_cache.h"
#include <algorithm>

namespace ems {
namespace net {

void RTDBConfigCache::loadYCConfigs(const std::vector<YCConfig>& configs) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    yc_configs_.clear();
    for (const auto& config : configs) {
        yc_configs_[config.code] = config;
    }
}

void RTDBConfigCache::loadYXConfigs(const std::vector<YXConfig>& configs) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    yx_configs_.clear();
    for (const auto& config : configs) {
        yx_configs_[config.code] = config;
    }
}

void RTDBConfigCache::loadYKConfigs(const std::vector<YKConfig>& configs) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    yk_configs_.clear();
    for (const auto& config : configs) {
        yk_configs_[config.code] = config;
    }
}

void RTDBConfigCache::loadYTConfigs(const std::vector<YTConfig>& configs) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    yt_configs_.clear();
    for (const auto& config : configs) {
        yt_configs_[config.code] = config;
    }
}

bool RTDBConfigCache::getYCConfig(const std::string& code, YCConfig& config) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = yc_configs_.find(code);
    if (it != yc_configs_.end()) {
        config = it->second;
        return true;
    }
    return false;
}

bool RTDBConfigCache::getYXConfig(const std::string& code, YXConfig& config) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = yx_configs_.find(code);
    if (it != yx_configs_.end()) {
        config = it->second;
        return true;
    }
    return false;
}

bool RTDBConfigCache::getYKConfig(const std::string& code, YKConfig& config) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = yk_configs_.find(code);
    if (it != yk_configs_.end()) {
        config = it->second;
        return true;
    }
    return false;
}

bool RTDBConfigCache::getYTConfig(const std::string& code, YTConfig& config) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = yt_configs_.find(code);
    if (it != yt_configs_.end()) {
        config = it->second;
        return true;
    }
    return false;
}

std::vector<std::string> RTDBConfigCache::getYCConfigCodes() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::string> codes;
    codes.reserve(yc_configs_.size());
    for (const auto& pair : yc_configs_) {
        codes.push_back(pair.first);
    }
    std::sort(codes.begin(), codes.end());
    return codes;
}

std::vector<std::string> RTDBConfigCache::getYXConfigCodes() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::string> codes;
    codes.reserve(yx_configs_.size());
    for (const auto& pair : yx_configs_) {
        codes.push_back(pair.first);
    }
    std::sort(codes.begin(), codes.end());
    return codes;
}

std::vector<std::string> RTDBConfigCache::getYKConfigCodes() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::string> codes;
    codes.reserve(yk_configs_.size());
    for (const auto& pair : yk_configs_) {
        codes.push_back(pair.first);
    }
    std::sort(codes.begin(), codes.end());
    return codes;
}

std::vector<std::string> RTDBConfigCache::getYTConfigCodes() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::string> codes;
    codes.reserve(yt_configs_.size());
    for (const auto& pair : yt_configs_) {
        codes.push_back(pair.first);
    }
    std::sort(codes.begin(), codes.end());
    return codes;
}

size_t RTDBConfigCache::getYCConfigCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return yc_configs_.size();
}

size_t RTDBConfigCache::getYXConfigCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return yx_configs_.size();
}

size_t RTDBConfigCache::getYKConfigCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return yk_configs_.size();
}

size_t RTDBConfigCache::getYTConfigCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return yt_configs_.size();
}

void RTDBConfigCache::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    yc_configs_.clear();
    yx_configs_.clear();
    yk_configs_.clear();
    yt_configs_.clear();
}

void RTDBConfigCache::clearYC() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    yc_configs_.clear();
}

void RTDBConfigCache::clearYX() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    yx_configs_.clear();
}

void RTDBConfigCache::clearYK() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    yk_configs_.clear();
}

void RTDBConfigCache::clearYT() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    yt_configs_.clear();
}

bool RTDBConfigCache::hasYCConfig(const std::string& code) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return yc_configs_.find(code) != yc_configs_.end();
}

bool RTDBConfigCache::hasYXConfig(const std::string& code) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return yx_configs_.find(code) != yx_configs_.end();
}

bool RTDBConfigCache::hasYKConfig(const std::string& code) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return yk_configs_.find(code) != yk_configs_.end();
}

bool RTDBConfigCache::hasYTConfig(const std::string& code) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return yt_configs_.find(code) != yt_configs_.end();
}

void RTDBConfigCache::setUpdateCallback(UpdateCallback callback) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    update_callback_ = callback;
}

void RTDBPointManager::setConfigCache(std::shared_ptr<RTDBConfigCache> cache) {
    config_cache_ = cache;
}

bool RTDBPointManager::getYCFullPoint(const std::string& code, 
                                      const YCPoint& data, 
                                      YCFullPoint& full_point) const {
    if (!config_cache_) {
        return false;
    }
    
    if (config_cache_->getYCConfig(code, full_point.config)) {
        full_point.data = data;
        return true;
    }
    return false;
}

bool RTDBPointManager::getYXFullPoint(const std::string& code, 
                                      const YXPoint& data, 
                                      YXFullPoint& full_point) const {
    if (!config_cache_) {
        return false;
    }
    
    if (config_cache_->getYXConfig(code, full_point.config)) {
        full_point.data = data;
        return true;
    }
    return false;
}

bool RTDBPointManager::getYKFullPoint(const std::string& code, 
                                      const YKPoint& data, 
                                      YKFullPoint& full_point) const {
    if (!config_cache_) {
        return false;
    }
    
    if (config_cache_->getYKConfig(code, full_point.config)) {
        full_point.data = data;
        return true;
    }
    return false;
}

bool RTDBPointManager::getYTFullPoint(const std::string& code, 
                                      const YTPoint& data, 
                                      YTFullPoint& full_point) const {
    if (!config_cache_) {
        return false;
    }
    
    if (config_cache_->getYTConfig(code, full_point.config)) {
        full_point.data = data;
        return true;
    }
    return false;
}

std::vector<RTDBPointManager::YCFullPoint> 
RTDBPointManager::getYCFullPoints(const std::vector<YCPoint>& data_points) {
    std::vector<YCFullPoint> result;
    
    if (!config_cache_) {
        return result;
    }
    
    auto codes = config_cache_->getYCConfigCodes();
    std::unordered_map<std::string, YCPoint> data_map;
    for (const auto& dp : data_points) {
        data_map[codes.empty() ? "" : ""] = dp;
    }
    
    for (const auto& code : codes) {
        auto data_it = data_map.find(code);
        if (data_it != data_map.end()) {
            YCFullPoint full_point;
            if (config_cache_->getYCConfig(code, full_point.config)) {
                full_point.data = data_it->second;
                result.push_back(full_point);
            }
        }
    }
    
    return result;
}

std::vector<RTDBPointManager::YXFullPoint> 
RTDBPointManager::getYXFullPoints(const std::vector<YXPoint>& data_points) {
    std::vector<YXFullPoint> result;
    
    if (!config_cache_) {
        return result;
    }
    
    auto codes = config_cache_->getYXConfigCodes();
    
    for (const auto& code : codes) {
        YXFullPoint full_point;
        if (config_cache_->getYXConfig(code, full_point.config)) {
            result.push_back(full_point);
        }
    }
    
    return result;
}

std::vector<RTDBPointManager::YKFullPoint> 
RTDBPointManager::getYKFullPoints(const std::vector<YKPoint>& data_points) {
    std::vector<YKFullPoint> result;
    
    if (!config_cache_) {
        return result;
    }
    
    auto codes = config_cache_->getYKConfigCodes();
    
    for (const auto& code : codes) {
        YKFullPoint full_point;
        if (config_cache_->getYKConfig(code, full_point.config)) {
            result.push_back(full_point);
        }
    }
    
    return result;
}

std::vector<RTDBPointManager::YTFullPoint> 
RTDBPointManager::getYTFullPoints(const std::vector<YTPoint>& data_points) {
    std::vector<YTFullPoint> result;
    
    if (!config_cache_) {
        return result;
    }
    
    auto codes = config_cache_->getYTConfigCodes();
    
    for (const auto& code : codes) {
        YTFullPoint full_point;
        if (config_cache_->getYTConfig(code, full_point.config)) {
            result.push_back(full_point);
        }
    }
    
    return result;
}

double RTDBPointManager::transformYCValue(double raw_value, 
                                         const RTDBConfigCache::YCConfig& config) const {
    return raw_value * config.mul_coe + config.add_coe;
}

double RTDBPointManager::transformYTValue(double raw_value, 
                                         const RTDBConfigCache::YTConfig& config) const {
    return raw_value * config.mul_coe + config.add_coe;
}

bool RTDBPointManager::validateYCValue(double value, 
                                       const RTDBConfigCache::YCConfig& config) const {
    return value >= config.min_limit && value <= config.max_limit;
}

bool RTDBPointManager::validateYTValue(double value, 
                                       const RTDBConfigCache::YTConfig& config) const {
    return value >= config.min_limit && value <= config.max_limit;
}

}
}
