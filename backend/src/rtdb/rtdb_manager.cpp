#include "rtdb/rtdb_manager.h"
#include <iostream>

namespace ems {
namespace net {

RtDbManager::RtDbManager()
    : rtdb_(nullptr)
    , config_cache_(nullptr)
    , point_manager_(nullptr) {
}

RtDbManager::~RtDbManager() {
    shutdown();
}

bool RtDbManager::initialize(const RtDbConfig& config) {
    rtdb_ = createRtDb(config);
    if (!rtdb_ || !rtdb_->isConnected()) {
        std::cerr << "Failed to connect to Redis: " 
                  << (rtdb_ ? rtdb_->getLastError() : "Unknown error") << std::endl;
        return false;
    }

    config_cache_ = std::make_shared<RtDbConfigCache>();
    point_manager_ = std::make_shared<RtDbPointManager>();
    point_manager_->setConfigCache(config_cache_);

    return true;
}

void RtDbManager::shutdown() {
    rtdb_.reset();
    config_cache_.reset();
    point_manager_.reset();
}

bool RtDbManager::loadYcConfigsFromDb() {
    return true;
}

bool RtDbManager::loadYxConfigsFromDb() {
    return true;
}

bool RtDbManager::loadYkConfigsFromDb() {
    return true;
}

bool RtDbManager::loadYtConfigsFromDb() {
    return true;
}

bool RtDbManager::loadAllConfigs() {
    if (!config_cache_) {
        return false;
    }

    if (!loadYcConfigsFromDb()) {
        return false;
    }
    if (!loadYxConfigsFromDb()) {
        return false;
    }
    if (!loadYkConfigsFromDb()) {
        return false;
    }
    if (!loadYtConfigsFromDb()) {
        return false;
    }

    return true;
}

void RtDbManager::setDataChangeCallback(DataChangeCallback callback) {
    data_change_callback_ = callback;
}

bool RtDbManager::updateYc(const std::string& code, const YcPoint& data) {
    if (!rtdb_) {
        return false;
    }

    bool result = rtdb_->setYc(code, data);
    
    if (result && data_change_callback_) {
        data_change_callback_("yc", code, &data);
    }
    
    return result;
}

bool RtDbManager::updateYx(const std::string& code, const YxPoint& data) {
    if (!rtdb_) {
        return false;
    }

    bool result = rtdb_->setYx(code, data);
    
    if (result && data_change_callback_) {
        data_change_callback_("yx", code, &data);
    }
    
    return result;
}

bool RtDbManager::updateYk(const std::string& code, const YkPoint& data) {
    if (!rtdb_) {
        return false;
    }

    bool result = rtdb_->setYk(code, data);
    
    if (result && data_change_callback_) {
        data_change_callback_("yk", code, &data);
    }
    
    return result;
}

bool RtDbManager::updateYt(const std::string& code, const YtPoint& data) {
    if (!rtdb_) {
        return false;
    }

    bool result = rtdb_->setYt(code, data);
    
    if (result && data_change_callback_) {
        data_change_callback_("yt", code, &data);
    }
    
    return result;
}

bool RtDbManager::batchUpdateYC(const std::map<std::string, YcPoint>& data_map) {
    if (!rtdb_) {
        return false;
    }

    for (const auto& pair : data_map) {
        if (!rtdb_->setYc(pair.first, pair.second)) {
            return false;
        }
    }

    if (data_change_callback_) {
        for (const auto& pair : data_map) {
            data_change_callback_("yc", pair.first, &pair.second);
        }
    }

    return true;
}

bool RtDbManager::batchUpdateYX(const std::map<std::string, YxPoint>& data_map) {
    if (!rtdb_) {
        return false;
    }

    for (const auto& pair : data_map) {
        if (!rtdb_->setYx(pair.first, pair.second)) {
            return false;
        }
    }

    if (data_change_callback_) {
        for (const auto& pair : data_map) {
            data_change_callback_("yx", pair.first, &pair.second);
        }
    }

    return true;
}

bool RtDbManager::batchUpdateYK(const std::map<std::string, YkPoint>& data_map) {
    if (!rtdb_) {
        return false;
    }

    for (const auto& pair : data_map) {
        if (!rtdb_->setYk(pair.first, pair.second)) {
            return false;
        }
    }

    if (data_change_callback_) {
        for (const auto& pair : data_map) {
            data_change_callback_("yk", pair.first, &pair.second);
        }
    }

    return true;
}

bool RtDbManager::batchUpdateYT(const std::map<std::string, YtPoint>& data_map) {
    if (!rtdb_) {
        return false;
    }

    for (const auto& pair : data_map) {
        if (!rtdb_->setYt(pair.first, pair.second)) {
            return false;
        }
    }

    if (data_change_callback_) {
        for (const auto& pair : data_map) {
            data_change_callback_("yt", pair.first, &pair.second);
        }
    }

    return true;
}

bool RtDbManager::getYc(const std::string& code, YcPoint& data) {
    if (!rtdb_) {
        return false;
    }
    return rtdb_->getYc(code, data);
}

bool RtDbManager::getYx(const std::string& code, YxPoint& data) {
    if (!rtdb_) {
        return false;
    }
    return rtdb_->getYx(code, data);
}

bool RtDbManager::getYk(const std::string& code, YkPoint& data) {
    if (!rtdb_) {
        return false;
    }
    return rtdb_->getYk(code, data);
}

bool RtDbManager::getYt(const std::string& code, YtPoint& data) {
    if (!rtdb_) {
        return false;
    }
    return rtdb_->getYt(code, data);
}

bool RtDbManager::getYcFull(const std::string& code, 
                            RtDbPointManager::YCFullPoint& fullPoint) {
    YcPoint data;
    if (getYc(code, data)) {
        return point_manager_->getYCFullPoint(code, data, fullPoint);
    }
    return false;
}

bool RtDbManager::getYxFull(const std::string& code, 
                            RtDbPointManager::YXFullPoint& fullPoint) {
    YxPoint data;
    if (getYx(code, data)) {
        return point_manager_->getYXFullPoint(code, data, fullPoint);
    }
    return false;
}

bool RtDbManager::getYkFull(const std::string& code, 
                            RtDbPointManager::YKFullPoint& fullPoint) {
    YkPoint data;
    if (getYk(code, data)) {
        return point_manager_->getYKFullPoint(code, data, fullPoint);
    }
    return false;
}

bool RtDbManager::getYtFull(const std::string& code, 
                            RtDbPointManager::YTFullPoint& fullPoint) {
    YtPoint data;
    if (getYt(code, data)) {
        return point_manager_->getYTFullPoint(code, data, fullPoint);
    }
    return false;
}

std::vector<YcPoint> RtDbManager::getAllYcData() {
    if (!rtdb_) {
        return {};
    }
    return rtdb_->getYCPoints("*");
}

std::vector<YxPoint> RtDbManager::getAllYxData() {
    if (!rtdb_) {
        return {};
    }
    return rtdb_->getYXPoints("*");
}

std::vector<YkPoint> RtDbManager::getAllYkData() {
    if (!rtdb_) {
        return {};
    }
    return rtdb_->getYKPoints("*");
}

std::vector<YtPoint> RtDbManager::getAllYtData() {
    if (!rtdb_) {
        return {};
    }
    return rtdb_->getYTPoints("*");
}

std::vector<RtDbPointManager::YCFullPoint> RtDbManager::getAllYcFullPoints() {
    std::vector<RtDbPointManager::YCFullPoint> result;
    
    if (!rtdb_ || !point_manager_ || !config_cache_) {
        return result;
    }

    auto codes = config_cache_->getYcConfigCodes();
    for (const auto& code : codes) {
        RtDbPointManager::YCFullPoint full_point;
        if (getYcFull(code, full_point)) {
            result.push_back(full_point);
        }
    }

    return result;
}

std::vector<RtDbPointManager::YXFullPoint> RtDbManager::getAllYxFullPoints() {
    std::vector<RtDbPointManager::YXFullPoint> result;
    
    if (!rtdb_ || !point_manager_ || !config_cache_) {
        return result;
    }

    auto codes = config_cache_->getYxConfigCodes();
    for (const auto& code : codes) {
        RtDbPointManager::YXFullPoint full_point;
        if (getYxFull(code, full_point)) {
            result.push_back(full_point);
        }
    }

    return result;
}

std::vector<RtDbPointManager::YKFullPoint> RtDbManager::getAllYkFullPoints() {
    std::vector<RtDbPointManager::YKFullPoint> result;
    
    if (!rtdb_ || !point_manager_ || !config_cache_) {
        return result;
    }

    auto codes = config_cache_->getYkConfigCodes();
    for (const auto& code : codes) {
        RtDbPointManager::YKFullPoint full_point;
        if (getYkFull(code, full_point)) {
            result.push_back(full_point);
        }
    }

    return result;
}

std::vector<RtDbPointManager::YTFullPoint> RtDbManager::getAllYtFullPoints() {
    std::vector<RtDbPointManager::YTFullPoint> result;
    
    if (!rtdb_ || !point_manager_ || !config_cache_) {
        return result;
    }

    auto codes = config_cache_->getYtConfigCodes();
    for (const auto& code : codes) {
        RtDbPointManager::YTFullPoint full_point;
        if (getYtFull(code, full_point)) {
            result.push_back(full_point);
        }
    }

    return result;
}

size_t RtDbManager::getYcDataCount() {
    if (!rtdb_) {
        return 0;
    }
    return rtdb_->getYcCodes("*").size();
}

size_t RtDbManager::getYxDataCount() {
    if (!rtdb_) {
        return 0;
    }
    return rtdb_->getYxCodes("*").size();
}

size_t RtDbManager::getYkDataCount() {
    if (!rtdb_) {
        return 0;
    }
    return rtdb_->getYkCodes("*").size();
}

size_t RtDbManager::getYtDataCount() {
    if (!rtdb_) {
        return 0;
    }
    return rtdb_->getYtCodes("*").size();
}

void RtDbManager::clearAllData() {
    if (!rtdb_) {
        return;
    }

    auto yc_codes = rtdb_->getYcCodes("*");
    for (const auto& code : yc_codes) {
        rtdb_->deleteYc(code);
    }

    auto yx_codes = rtdb_->getYxCodes("*");
    for (const auto& code : yx_codes) {
        rtdb_->deleteYx(code);
    }

    auto yk_codes = rtdb_->getYkCodes("*");
    for (const auto& code : yk_codes) {
        rtdb_->deleteYk(code);
    }

    auto yt_codes = rtdb_->getYtCodes("*");
    for (const auto& code : yt_codes) {
        rtdb_->deleteYt(code);
    }
}

std::unique_ptr<RtDbManager> createRTDBManager(const RtDbConfig& config) {
    auto manager = std::make_unique<RtDbManager>();
    if (manager->initialize(config)) {
        return manager;
    }
    return nullptr;
}

}
}
