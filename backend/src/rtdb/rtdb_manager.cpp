#include "rtdb/rtdb_manager.h"
#include <iostream>

namespace ems {
namespace net {

RTDBManager::RTDBManager()
    : rtdb_(nullptr)
    , config_cache_(nullptr)
    , point_manager_(nullptr) {
}

RTDBManager::~RTDBManager() {
    shutdown();
}

bool RTDBManager::initialize(const RTDBConfig& config) {
    rtdb_ = createRTDB(config);
    if (!rtdb_ || !rtdb_->isConnected()) {
        std::cerr << "Failed to connect to Redis: " 
                  << (rtdb_ ? rtdb_->getLastError() : "Unknown error") << std::endl;
        return false;
    }

    config_cache_ = std::make_shared<RTDBConfigCache>();
    point_manager_ = std::make_shared<RTDBPointManager>();
    point_manager_->setConfigCache(config_cache_);

    return true;
}

void RTDBManager::shutdown() {
    rtdb_.reset();
    config_cache_.reset();
    point_manager_.reset();
}

bool RTDBManager::loadYCConfigsFromDB() {
    return true;
}

bool RTDBManager::loadYXConfigsFromDB() {
    return true;
}

bool RTDBManager::loadYKConfigsFromDB() {
    return true;
}

bool RTDBManager::loadYTConfigsFromDB() {
    return true;
}

bool RTDBManager::loadAllConfigs() {
    if (!config_cache_) {
        return false;
    }

    if (!loadYCConfigsFromDB()) {
        return false;
    }
    if (!loadYXConfigsFromDB()) {
        return false;
    }
    if (!loadYKConfigsFromDB()) {
        return false;
    }
    if (!loadYTConfigsFromDB()) {
        return false;
    }

    return true;
}

void RTDBManager::setDataChangeCallback(DataChangeCallback callback) {
    data_change_callback_ = callback;
}

bool RTDBManager::updateYC(const std::string& code, const YCPoint& data) {
    if (!rtdb_) {
        return false;
    }

    bool result = rtdb_->setYC(code, data);
    
    if (result && data_change_callback_) {
        data_change_callback_("yc", code, &data);
    }
    
    return result;
}

bool RTDBManager::updateYX(const std::string& code, const YXPoint& data) {
    if (!rtdb_) {
        return false;
    }

    bool result = rtdb_->setYX(code, data);
    
    if (result && data_change_callback_) {
        data_change_callback_("yx", code, &data);
    }
    
    return result;
}

bool RTDBManager::updateYK(const std::string& code, const YKPoint& data) {
    if (!rtdb_) {
        return false;
    }

    bool result = rtdb_->setYK(code, data);
    
    if (result && data_change_callback_) {
        data_change_callback_("yk", code, &data);
    }
    
    return result;
}

bool RTDBManager::updateYT(const std::string& code, const YTPoint& data) {
    if (!rtdb_) {
        return false;
    }

    bool result = rtdb_->setYT(code, data);
    
    if (result && data_change_callback_) {
        data_change_callback_("yt", code, &data);
    }
    
    return result;
}

bool RTDBManager::batchUpdateYC(const std::map<std::string, YCPoint>& data_map) {
    if (!rtdb_) {
        return false;
    }

    for (const auto& pair : data_map) {
        if (!rtdb_->setYC(pair.first, pair.second)) {
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

bool RTDBManager::batchUpdateYX(const std::map<std::string, YXPoint>& data_map) {
    if (!rtdb_) {
        return false;
    }

    for (const auto& pair : data_map) {
        if (!rtdb_->setYX(pair.first, pair.second)) {
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

bool RTDBManager::batchUpdateYK(const std::map<std::string, YKPoint>& data_map) {
    if (!rtdb_) {
        return false;
    }

    for (const auto& pair : data_map) {
        if (!rtdb_->setYK(pair.first, pair.second)) {
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

bool RTDBManager::batchUpdateYT(const std::map<std::string, YTPoint>& data_map) {
    if (!rtdb_) {
        return false;
    }

    for (const auto& pair : data_map) {
        if (!rtdb_->setYT(pair.first, pair.second)) {
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

bool RTDBManager::getYC(const std::string& code, YCPoint& data) {
    if (!rtdb_) {
        return false;
    }
    return rtdb_->getYC(code, data);
}

bool RTDBManager::getYX(const std::string& code, YXPoint& data) {
    if (!rtdb_) {
        return false;
    }
    return rtdb_->getYX(code, data);
}

bool RTDBManager::getYK(const std::string& code, YKPoint& data) {
    if (!rtdb_) {
        return false;
    }
    return rtdb_->getYK(code, data);
}

bool RTDBManager::getYT(const std::string& code, YTPoint& data) {
    if (!rtdb_) {
        return false;
    }
    return rtdb_->getYT(code, data);
}

bool RTDBManager::getYCFull(const std::string& code, 
                            RTDBPointManager::YCFullPoint& full_point) {
    YCPoint data;
    if (getYC(code, data)) {
        return point_manager_->getYCFullPoint(code, data, full_point);
    }
    return false;
}

bool RTDBManager::getYXFull(const std::string& code, 
                            RTDBPointManager::YXFullPoint& full_point) {
    YXPoint data;
    if (getYX(code, data)) {
        return point_manager_->getYXFullPoint(code, data, full_point);
    }
    return false;
}

bool RTDBManager::getYKFull(const std::string& code, 
                            RTDBPointManager::YKFullPoint& full_point) {
    YKPoint data;
    if (getYK(code, data)) {
        return point_manager_->getYKFullPoint(code, data, full_point);
    }
    return false;
}

bool RTDBManager::getYTFull(const std::string& code, 
                            RTDBPointManager::YTFullPoint& full_point) {
    YTPoint data;
    if (getYT(code, data)) {
        return point_manager_->getYTFullPoint(code, data, full_point);
    }
    return false;
}

std::vector<YCPoint> RTDBManager::getAllYCData() {
    if (!rtdb_) {
        return {};
    }
    return rtdb_->getYCPoints("*");
}

std::vector<YXPoint> RTDBManager::getAllYXData() {
    if (!rtdb_) {
        return {};
    }
    return rtdb_->getYXPoints("*");
}

std::vector<YKPoint> RTDBManager::getAllYKData() {
    if (!rtdb_) {
        return {};
    }
    return rtdb_->getYKPoints("*");
}

std::vector<YTPoint> RTDBManager::getAllYTData() {
    if (!rtdb_) {
        return {};
    }
    return rtdb_->getYTPoints("*");
}

std::vector<RTDBPointManager::YCFullPoint> RTDBManager::getAllYCFullPoints() {
    std::vector<RTDBPointManager::YCFullPoint> result;
    
    if (!rtdb_ || !point_manager_ || !config_cache_) {
        return result;
    }

    auto codes = config_cache_->getYCConfigCodes();
    for (const auto& code : codes) {
        RTDBPointManager::YCFullPoint full_point;
        if (getYCFull(code, full_point)) {
            result.push_back(full_point);
        }
    }

    return result;
}

std::vector<RTDBPointManager::YXFullPoint> RTDBManager::getAllYXFullPoints() {
    std::vector<RTDBPointManager::YXFullPoint> result;
    
    if (!rtdb_ || !point_manager_ || !config_cache_) {
        return result;
    }

    auto codes = config_cache_->getYXConfigCodes();
    for (const auto& code : codes) {
        RTDBPointManager::YXFullPoint full_point;
        if (getYXFull(code, full_point)) {
            result.push_back(full_point);
        }
    }

    return result;
}

std::vector<RTDBPointManager::YKFullPoint> RTDBManager::getAllYKFullPoints() {
    std::vector<RTDBPointManager::YKFullPoint> result;
    
    if (!rtdb_ || !point_manager_ || !config_cache_) {
        return result;
    }

    auto codes = config_cache_->getYKConfigCodes();
    for (const auto& code : codes) {
        RTDBPointManager::YKFullPoint full_point;
        if (getYKFull(code, full_point)) {
            result.push_back(full_point);
        }
    }

    return result;
}

std::vector<RTDBPointManager::YTFullPoint> RTDBManager::getAllYTFullPoints() {
    std::vector<RTDBPointManager::YTFullPoint> result;
    
    if (!rtdb_ || !point_manager_ || !config_cache_) {
        return result;
    }

    auto codes = config_cache_->getYTConfigCodes();
    for (const auto& code : codes) {
        RTDBPointManager::YTFullPoint full_point;
        if (getYTFull(code, full_point)) {
            result.push_back(full_point);
        }
    }

    return result;
}

size_t RTDBManager::getYCDataCount() {
    if (!rtdb_) {
        return 0;
    }
    return rtdb_->getYCCodes("*").size();
}

size_t RTDBManager::getYXDataCount() {
    if (!rtdb_) {
        return 0;
    }
    return rtdb_->getYXCodes("*").size();
}

size_t RTDBManager::getYKDataCount() {
    if (!rtdb_) {
        return 0;
    }
    return rtdb_->getYKCodes("*").size();
}

size_t RTDBManager::getYTDataCount() {
    if (!rtdb_) {
        return 0;
    }
    return rtdb_->getYTCodes("*").size();
}

void RTDBManager::clearAllData() {
    if (!rtdb_) {
        return;
    }

    auto yc_codes = rtdb_->getYCCodes("*");
    for (const auto& code : yc_codes) {
        rtdb_->deleteYC(code);
    }

    auto yx_codes = rtdb_->getYXCodes("*");
    for (const auto& code : yx_codes) {
        rtdb_->deleteYX(code);
    }

    auto yk_codes = rtdb_->getYKCodes("*");
    for (const auto& code : yk_codes) {
        rtdb_->deleteYK(code);
    }

    auto yt_codes = rtdb_->getYTCodes("*");
    for (const auto& code : yt_codes) {
        rtdb_->deleteYT(code);
    }
}

std::unique_ptr<RTDBManager> createRTDBManager(const RTDBConfig& config) {
    auto manager = std::make_unique<RTDBManager>();
    if (manager->initialize(config)) {
        return manager;
    }
    return nullptr;
}

}
}
