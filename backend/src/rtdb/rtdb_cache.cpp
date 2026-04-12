#include "rtdb/rtdb_cache.h"
#include <algorithm>

namespace ems {
namespace net {

void RtDbConfigCache::loadYcConfigs(const std::vector<YcConfig>& configs) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    yc_configs_.clear();
    for (const auto& config : configs) {
        yc_configs_[config.code] = config;
    }
}

void RtDbConfigCache::loadYxConfigs(const std::vector<YxConfig>& configs) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    yx_configs_.clear();
    for (const auto& config : configs) {
        yx_configs_[config.code] = config;
    }
}

void RtDbConfigCache::loadYkConfigs(const std::vector<YkConfig>& configs) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    yk_configs_.clear();
    for (const auto& config : configs) {
        yk_configs_[config.code] = config;
    }
}

void RtDbConfigCache::loadYtConfigs(const std::vector<YtConfig>& configs) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    yt_configs_.clear();
    for (const auto& config : configs) {
        yt_configs_[config.code] = config;
    }
}

bool RtDbConfigCache::getYcConfig(const std::string& code, YcConfig& config) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = yc_configs_.find(code);
    if (it != yc_configs_.end()) {
        config = it->second;
        return true;
    }
    return false;
}

bool RtDbConfigCache::getYxConfig(const std::string& code, YxConfig& config) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = yx_configs_.find(code);
    if (it != yx_configs_.end()) {
        config = it->second;
        return true;
    }
    return false;
}

bool RtDbConfigCache::getYkConfig(const std::string& code, YkConfig& config) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = yk_configs_.find(code);
    if (it != yk_configs_.end()) {
        config = it->second;
        return true;
    }
    return false;
}

bool RtDbConfigCache::getYtConfig(const std::string& code, YtConfig& config) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = yt_configs_.find(code);
    if (it != yt_configs_.end()) {
        config = it->second;
        return true;
    }
    return false;
}

std::vector<std::string> RtDbConfigCache::getYcConfigCodes() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::string> codes;
    codes.reserve(yc_configs_.size());
    for (const auto& pair : yc_configs_) {
        codes.push_back(pair.first);
    }
    std::sort(codes.begin(), codes.end());
    return codes;
}

std::vector<std::string> RtDbConfigCache::getYxConfigCodes() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::string> codes;
    codes.reserve(yx_configs_.size());
    for (const auto& pair : yx_configs_) {
        codes.push_back(pair.first);
    }
    std::sort(codes.begin(), codes.end());
    return codes;
}

std::vector<std::string> RtDbConfigCache::getYkConfigCodes() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::string> codes;
    codes.reserve(yk_configs_.size());
    for (const auto& pair : yk_configs_) {
        codes.push_back(pair.first);
    }
    std::sort(codes.begin(), codes.end());
    return codes;
}

std::vector<std::string> RtDbConfigCache::getYtConfigCodes() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::string> codes;
    codes.reserve(yt_configs_.size());
    for (const auto& pair : yt_configs_) {
        codes.push_back(pair.first);
    }
    std::sort(codes.begin(), codes.end());
    return codes;
}

size_t RtDbConfigCache::getYcConfigCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return yc_configs_.size();
}

size_t RtDbConfigCache::getYxConfigCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return yx_configs_.size();
}

size_t RtDbConfigCache::getYkConfigCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return yk_configs_.size();
}

size_t RtDbConfigCache::getYtConfigCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return yt_configs_.size();
}

void RtDbConfigCache::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    yc_configs_.clear();
    yx_configs_.clear();
    yk_configs_.clear();
    yt_configs_.clear();
}

void RtDbConfigCache::clearYc() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    yc_configs_.clear();
}

void RtDbConfigCache::clearYx() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    yx_configs_.clear();
}

void RtDbConfigCache::clearYk() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    yk_configs_.clear();
}

void RtDbConfigCache::clearYt() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    yt_configs_.clear();
}

bool RtDbConfigCache::hasYcConfig(const std::string& code) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return yc_configs_.find(code) != yc_configs_.end();
}

bool RtDbConfigCache::hasYxConfig(const std::string& code) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return yx_configs_.find(code) != yx_configs_.end();
}

bool RtDbConfigCache::hasYkConfig(const std::string& code) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return yk_configs_.find(code) != yk_configs_.end();
}

bool RtDbConfigCache::hasYtConfig(const std::string& code) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return yt_configs_.find(code) != yt_configs_.end();
}

void RtDbConfigCache::setUpdateCallback(UpdateCallback callback) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    update_callback_ = callback;
}

void RtDbPointManager::setConfigCache(std::shared_ptr<RtDbConfigCache> cache) {
    config_cache_ = cache;
}

bool RtDbPointManager::getYCFullPoint(const std::string& code, 
                                      const YcPoint& data, 
                                      YCFullPoint& full_point) const {
    if (!config_cache_) {
        return false;
    }
    
    if (config_cache_->getYcConfig(code, full_point.config)) {
        full_point.data = data;
        return true;
    }
    return false;
}

bool RtDbPointManager::getYXFullPoint(const std::string& code, 
                                      const YxPoint& data, 
                                      YXFullPoint& full_point) const {
    if (!config_cache_) {
        return false;
    }
    
    if (config_cache_->getYxConfig(code, full_point.config)) {
        full_point.data = data;
        return true;
    }
    return false;
}

bool RtDbPointManager::getYKFullPoint(const std::string& code, 
                                      const YkPoint& data, 
                                      YKFullPoint& full_point) const {
    if (!config_cache_) {
        return false;
    }
    
    if (config_cache_->getYkConfig(code, full_point.config)) {
        full_point.data = data;
        return true;
    }
    return false;
}

bool RtDbPointManager::getYTFullPoint(const std::string& code, 
                                      const YtPoint& data, 
                                      YTFullPoint& full_point) const {
    if (!config_cache_) {
        return false;
    }
    
    if (config_cache_->getYtConfig(code, full_point.config)) {
        full_point.data = data;
        return true;
    }
    return false;
}

std::vector<RtDbPointManager::YCFullPoint> 
RtDbPointManager::getYCFullPoints(const std::vector<YcPoint>& data_points) {
    std::vector<YCFullPoint> result;
    
    if (!config_cache_) {
        return result;
    }
    
    auto codes = config_cache_->getYcConfigCodes();
    std::unordered_map<std::string, YcPoint> data_map;
    for (const auto& dp : data_points) {
        data_map[codes.empty() ? "" : ""] = dp;
    }
    
    for (const auto& code : codes) {
        auto data_it = data_map.find(code);
        if (data_it != data_map.end()) {
            YCFullPoint full_point;
            if (config_cache_->getYcConfig(code, full_point.config)) {
                full_point.data = data_it->second;
                result.push_back(full_point);
            }
        }
    }
    
    return result;
}

std::vector<RtDbPointManager::YXFullPoint> 
RtDbPointManager::getYXFullPoints(const std::vector<YxPoint>& data_points) {
    std::vector<YXFullPoint> result;
    
    if (!config_cache_) {
        return result;
    }
    
    auto codes = config_cache_->getYxConfigCodes();
    
    for (const auto& code : codes) {
        YXFullPoint full_point;
        if (config_cache_->getYxConfig(code, full_point.config)) {
            result.push_back(full_point);
        }
    }
    
    return result;
}

std::vector<RtDbPointManager::YKFullPoint> 
RtDbPointManager::getYKFullPoints(const std::vector<YkPoint>& data_points) {
    std::vector<YKFullPoint> result;
    
    if (!config_cache_) {
        return result;
    }
    
    auto codes = config_cache_->getYkConfigCodes();
    
    for (const auto& code : codes) {
        YKFullPoint full_point;
        if (config_cache_->getYkConfig(code, full_point.config)) {
            result.push_back(full_point);
        }
    }
    
    return result;
}

std::vector<RtDbPointManager::YTFullPoint> 
RtDbPointManager::getYTFullPoints(const std::vector<YtPoint>& data_points) {
    std::vector<YTFullPoint> result;
    
    if (!config_cache_) {
        return result;
    }
    
    auto codes = config_cache_->getYtConfigCodes();
    
    for (const auto& code : codes) {
        YTFullPoint full_point;
        if (config_cache_->getYtConfig(code, full_point.config)) {
            result.push_back(full_point);
        }
    }
    
    return result;
}

double RtDbPointManager::transformYCValue(double raw_value, 
                                         const RtDbConfigCache::YcConfig& config) const {
    return raw_value * config.mul_coe + config.add_coe;
}

double RtDbPointManager::transformYTValue(double raw_value, 
                                         const RtDbConfigCache::YtConfig& config) const {
    return raw_value * config.mul_coe + config.add_coe;
}

bool RtDbPointManager::validateYCValue(double value, 
                                       const RtDbConfigCache::YcConfig& config) const {
    return value >= config.min_limit && value <= config.max_limit;
}

bool RtDbPointManager::validateYTValue(double value, 
                                       const RtDbConfigCache::YtConfig& config) const {
    return value >= config.min_limit && value <= config.max_limit;
}

}
}
