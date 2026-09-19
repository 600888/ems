#include "rtdb/rtdb_cache.h"

#include <algorithm>

namespace ems {
namespace rtdb {

void RtDbConfigCache::loadYcConfigs(const std::vector<YcConfig>& configs) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    ycConfigs_.clear();
    for (const auto& config : configs) {
        ycConfigs_[config.code] = config;
    }
}

void RtDbConfigCache::loadYxConfigs(const std::vector<YxConfig>& configs) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    yxConfigs_.clear();
    for (const auto& config : configs) {
        yxConfigs_[config.code] = config;
    }
}

void RtDbConfigCache::loadYkConfigs(const std::vector<YkConfig>& configs) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    ykConfigs_.clear();
    for (const auto& config : configs) {
        ykConfigs_[config.code] = config;
    }
}

void RtDbConfigCache::loadYtConfigs(const std::vector<YtConfig>& configs) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    ytConfigs_.clear();
    for (const auto& config : configs) {
        ytConfigs_[config.code] = config;
    }
}

bool RtDbConfigCache::getYcConfig(const std::string& code, YcConfig& config) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = ycConfigs_.find(code);
    if (it != ycConfigs_.end()) {
        config = it->second;
        return true;
    }
    return false;
}

bool RtDbConfigCache::getYxConfig(const std::string& code, YxConfig& config) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = yxConfigs_.find(code);
    if (it != yxConfigs_.end()) {
        config = it->second;
        return true;
    }
    return false;
}

bool RtDbConfigCache::getYkConfig(const std::string& code, YkConfig& config) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = ykConfigs_.find(code);
    if (it != ykConfigs_.end()) {
        config = it->second;
        return true;
    }
    return false;
}

bool RtDbConfigCache::getYtConfig(const std::string& code, YtConfig& config) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = ytConfigs_.find(code);
    if (it != ytConfigs_.end()) {
        config = it->second;
        return true;
    }
    return false;
}

std::vector<std::string> RtDbConfigCache::getYcConfigCodes() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::string> codes;
    codes.reserve(ycConfigs_.size());
    for (const auto& pair : ycConfigs_) {
        codes.push_back(pair.first);
    }
    std::sort(codes.begin(), codes.end());
    return codes;
}

std::vector<std::string> RtDbConfigCache::getYxConfigCodes() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::string> codes;
    codes.reserve(yxConfigs_.size());
    for (const auto& pair : yxConfigs_) {
        codes.push_back(pair.first);
    }
    std::sort(codes.begin(), codes.end());
    return codes;
}

std::vector<std::string> RtDbConfigCache::getYkConfigCodes() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::string> codes;
    codes.reserve(ykConfigs_.size());
    for (const auto& pair : ykConfigs_) {
        codes.push_back(pair.first);
    }
    std::sort(codes.begin(), codes.end());
    return codes;
}

std::vector<std::string> RtDbConfigCache::getYtConfigCodes() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::string> codes;
    codes.reserve(ytConfigs_.size());
    for (const auto& pair : ytConfigs_) {
        codes.push_back(pair.first);
    }
    std::sort(codes.begin(), codes.end());
    return codes;
}

size_t RtDbConfigCache::getYcConfigCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return ycConfigs_.size();
}

size_t RtDbConfigCache::getYxConfigCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return yxConfigs_.size();
}

size_t RtDbConfigCache::getYkConfigCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return ykConfigs_.size();
}

size_t RtDbConfigCache::getYtConfigCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return ytConfigs_.size();
}

void RtDbConfigCache::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    ycConfigs_.clear();
    yxConfigs_.clear();
    ykConfigs_.clear();
    ytConfigs_.clear();
}

void RtDbConfigCache::clearYc() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    ycConfigs_.clear();
}

void RtDbConfigCache::clearYx() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    yxConfigs_.clear();
}

void RtDbConfigCache::clearYk() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    ykConfigs_.clear();
}

void RtDbConfigCache::clearYt() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    ytConfigs_.clear();
}

bool RtDbConfigCache::hasYcConfig(const std::string& code) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return ycConfigs_.find(code) != ycConfigs_.end();
}

bool RtDbConfigCache::hasYxConfig(const std::string& code) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return yxConfigs_.find(code) != yxConfigs_.end();
}

bool RtDbConfigCache::hasYkConfig(const std::string& code) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return ykConfigs_.find(code) != ykConfigs_.end();
}

bool RtDbConfigCache::hasYtConfig(const std::string& code) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return ytConfigs_.find(code) != ytConfigs_.end();
}

void RtDbConfigCache::setUpdateCallback(UpdateCallback callback) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    updateCallback_ = callback;
}

bool RtDbConfigCache::getPointConfig(const std::string& code, PointConfig& config) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    {
        auto it = ycConfigs_.find(code);
        if (it != ycConfigs_.end()) {
            config = toPointConfig(it->second);
            return true;
        }
    }
    {
        auto it = yxConfigs_.find(code);
        if (it != yxConfigs_.end()) {
            config = toPointConfig(it->second);
            return true;
        }
    }
    {
        auto it = ykConfigs_.find(code);
        if (it != ykConfigs_.end()) {
            config = toPointConfig(it->second);
            return true;
        }
    }
    {
        auto it = ytConfigs_.find(code);
        if (it != ytConfigs_.end()) {
            config = toPointConfig(it->second);
            return true;
        }
    }
    return false;
}

bool RtDbConfigCache::setPointConfig(const PointConfig& config) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    switch (config.pointType_) {
        case PointType::YC:
            ycConfigs_[config.code_] = toYcConfig(config);
            break;
        case PointType::YX:
            yxConfigs_[config.code_] = toYxConfig(config);
            break;
        case PointType::YK:
            ykConfigs_[config.code_] = toYkConfig(config);
            break;
        case PointType::YT:
            ytConfigs_[config.code_] = toYtConfig(config);
            break;
    }
    return true;
}

bool RtDbConfigCache::deletePointConfig(const std::string& code) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    return ycConfigs_.erase(code) > 0 || yxConfigs_.erase(code) > 0 || ykConfigs_.erase(code) > 0 ||
           ytConfigs_.erase(code) > 0;
}

std::vector<PointConfig> RtDbConfigCache::getAllPointConfigs() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<PointConfig> results;
    results.reserve(ycConfigs_.size() + yxConfigs_.size() + ykConfigs_.size() + ytConfigs_.size());

    for (const auto& [_, cfg] : ycConfigs_) {
        results.push_back(toPointConfig(cfg));
    }
    for (const auto& [_, cfg] : yxConfigs_) {
        results.push_back(toPointConfig(cfg));
    }
    for (const auto& [_, cfg] : ykConfigs_) {
        results.push_back(toPointConfig(cfg));
    }
    for (const auto& [_, cfg] : ytConfigs_) {
        results.push_back(toPointConfig(cfg));
    }

    return results;
}

RtDbConfigCache::YcConfig RtDbConfigCache::toYcConfig(const PointConfig& pc) const {
    YcConfig cfg;
    cfg.code = pc.code_;
    cfg.name = pc.name_;
    cfg.channel_id = pc.channelId_;
    cfg.mulCoe = pc.mulCoe_;
    cfg.addCoe = pc.addCoe_;
    cfg.maxLimit = pc.maxLimit_;
    cfg.minLimit = pc.minLimit_;
    cfg.unit = pc.unit_;
    cfg.status = pc.status_;
    return cfg;
}

RtDbConfigCache::YxConfig RtDbConfigCache::toYxConfig(const PointConfig& pc) const {
    YxConfig cfg;
    cfg.code = pc.code_;
    cfg.name = pc.name_;
    cfg.channel_id = pc.channelId_;
    cfg.bit = pc.bit_;
    cfg.reverse = pc.reverse_;
    cfg.status = pc.status_;
    return cfg;
}

RtDbConfigCache::YkConfig RtDbConfigCache::toYkConfig(const PointConfig& pc) const {
    YkConfig cfg;
    cfg.code = pc.code_;
    cfg.name = pc.name_;
    cfg.channel_id = pc.channelId_;
    cfg.bit = pc.bit_;
    cfg.commandType = pc.commandType_;
    cfg.status = pc.status_;
    return cfg;
}

RtDbConfigCache::YtConfig RtDbConfigCache::toYtConfig(const PointConfig& pc) const {
    YtConfig cfg;
    cfg.code = pc.code_;
    cfg.name = pc.name_;
    cfg.channel_id = pc.channelId_;
    cfg.mulCoe = pc.mulCoe_;
    cfg.addCoe = pc.addCoe_;
    cfg.maxLimit = pc.maxLimit_;
    cfg.minLimit = pc.minLimit_;
    cfg.unit = pc.unit_;
    cfg.status = pc.status_;
    return cfg;
}

PointConfig RtDbConfigCache::toPointConfig(const YcConfig& yc) const {
    PointConfig pc;
    pc.code_ = yc.code;
    pc.name_ = yc.name;
    pc.pointType_ = PointType::YC;
    pc.channelId_ = yc.channel_id;
    pc.mulCoe_ = yc.mulCoe;
    pc.addCoe_ = yc.addCoe;
    pc.minLimit_ = yc.minLimit;
    pc.maxLimit_ = yc.maxLimit;
    pc.unit_ = yc.unit;
    pc.status_ = yc.status;
    return pc;
}

PointConfig RtDbConfigCache::toPointConfig(const YxConfig& yx) const {
    PointConfig pc;
    pc.code_ = yx.code;
    pc.name_ = yx.name;
    pc.pointType_ = PointType::YX;
    pc.channelId_ = yx.channel_id;
    pc.bit_ = yx.bit;
    pc.reverse_ = yx.reverse;
    pc.status_ = yx.status;
    return pc;
}

PointConfig RtDbConfigCache::toPointConfig(const YkConfig& yk) const {
    PointConfig pc;
    pc.code_ = yk.code;
    pc.name_ = yk.name;
    pc.pointType_ = PointType::YK;
    pc.channelId_ = yk.channel_id;
    pc.bit_ = yk.bit;
    pc.commandType_ = yk.commandType;
    pc.status_ = yk.status;
    return pc;
}

PointConfig RtDbConfigCache::toPointConfig(const YtConfig& yt) const {
    PointConfig pc;
    pc.code_ = yt.code;
    pc.name_ = yt.name;
    pc.pointType_ = PointType::YT;
    pc.channelId_ = yt.channel_id;
    pc.mulCoe_ = yt.mulCoe;
    pc.addCoe_ = yt.addCoe;
    pc.minLimit_ = yt.minLimit;
    pc.maxLimit_ = yt.maxLimit;
    pc.unit_ = yt.unit;
    pc.status_ = yt.status;
    return pc;
}

void RtDbPointManager::setConfigCache(std::shared_ptr<RtDbConfigCache> cache) {
    configCache_ = cache;
}

bool RtDbPointManager::getYcFullPoint(const std::string& code, const YcPoint& data,
                                      YcFullPoint& fullPoint) const {
    if (!configCache_) {
        return false;
    }

    if (configCache_->getYcConfig(code, fullPoint.config)) {
        fullPoint.data = data;
        return true;
    }
    return false;
}

bool RtDbPointManager::getYxFullPoint(const std::string& code, const YxPoint& data,
                                      YxFullPoint& fullPoint) const {
    if (!configCache_) {
        return false;
    }

    if (configCache_->getYxConfig(code, fullPoint.config)) {
        fullPoint.data = data;
        return true;
    }
    return false;
}

bool RtDbPointManager::getYkFullPoint(const std::string& code, const YkPoint& data,
                                      YkFullPoint& fullPoint) const {
    if (!configCache_) {
        return false;
    }

    if (configCache_->getYkConfig(code, fullPoint.config)) {
        fullPoint.data = data;
        return true;
    }
    return false;
}

bool RtDbPointManager::getYtFullPoint(const std::string& code, const YtPoint& data,
                                      YtFullPoint& fullPoint) const {
    if (!configCache_) {
        return false;
    }

    if (configCache_->getYtConfig(code, fullPoint.config)) {
        fullPoint.data = data;
        return true;
    }
    return false;
}

std::vector<RtDbPointManager::YcFullPoint> RtDbPointManager::getYcFullPoints(
    const std::vector<YcPoint>& dataPoints) {
    std::vector<YcFullPoint> result;

    if (!configCache_) {
        return result;
    }

    for (const auto& data : dataPoints) {
        YcFullPoint fullPoint;
        if (configCache_->getYcConfig(data.code_, fullPoint.config)) {
            fullPoint.data = data;
            result.push_back(fullPoint);
        }
    }

    return result;
}

std::vector<RtDbPointManager::YxFullPoint> RtDbPointManager::getYxFullPoints(
    const std::vector<YxPoint>& dataPoints) {
    std::vector<YxFullPoint> result;

    if (!configCache_) {
        return result;
    }

    for (const auto& data : dataPoints) {
        YxFullPoint fullPoint;
        if (configCache_->getYxConfig(data.code_, fullPoint.config)) {
            fullPoint.data = data;
            result.push_back(fullPoint);
        }
    }

    return result;
}

std::vector<RtDbPointManager::YkFullPoint> RtDbPointManager::getYkFullPoints(
    const std::vector<YkPoint>& dataPoints) {
    std::vector<YkFullPoint> result;

    if (!configCache_) {
        return result;
    }

    for (const auto& data : dataPoints) {
        YkFullPoint fullPoint;
        if (configCache_->getYkConfig(data.code_, fullPoint.config)) {
            fullPoint.data = data;
            result.push_back(fullPoint);
        }
    }

    return result;
}

std::vector<RtDbPointManager::YtFullPoint> RtDbPointManager::getYtFullPoints(
    const std::vector<YtPoint>& dataPoints) {
    std::vector<YtFullPoint> result;

    if (!configCache_) {
        return result;
    }

    for (const auto& data : dataPoints) {
        YtFullPoint fullPoint;
        if (configCache_->getYtConfig(data.code_, fullPoint.config)) {
            fullPoint.data = data;
            result.push_back(fullPoint);
        }
    }

    return result;
}

double RtDbPointManager::transformYcValue(double rawValue,
                                          const RtDbConfigCache::YcConfig& config) const {
    return rawValue * config.mulCoe + config.addCoe;
}

double RtDbPointManager::transformYtValue(double rawValue,
                                          const RtDbConfigCache::YtConfig& config) const {
    return rawValue * config.mulCoe + config.addCoe;
}

bool RtDbPointManager::validateYcValue(double value,
                                       const RtDbConfigCache::YcConfig& config) const {
    return value >= config.minLimit && value <= config.maxLimit;
}

bool RtDbPointManager::validateYtValue(double value,
                                       const RtDbConfigCache::YtConfig& config) const {
    return value >= config.minLimit && value <= config.maxLimit;
}

}  // namespace rtdb
}  // namespace ems