#include "rtdb/rtdb_manager.h"

#include <fstream>
#include <iostream>
#include <sstream>

namespace ems {
namespace rtdb {

RtDbManager::RtDbManager() : rtdb_(nullptr), configCache_(nullptr), pointManager_(nullptr) {}

RtDbManager::~RtDbManager() { shutdown(); }

bool RtDbManager::initialize(const RtDbConfig& config) {
    rtdb_ = createRtDb(config);
    if (!rtdb_ || !rtdb_->isConnected()) {
        std::cerr << "Failed to connect to Redis: "
                  << (rtdb_ ? rtdb_->getLastError() : "Unknown error") << std::endl;
        return false;
    }

    configCache_ = std::make_shared<RtDbConfigCache>();
    pointManager_ = std::make_shared<RtDbPointManager>();
    pointManager_->setConfigCache(configCache_);

    return true;
}

void RtDbManager::shutdown() {
    rtdb_.reset();
    configCache_.reset();
    pointManager_.reset();
}

bool RtDbManager::loadYcConfigsFromDb() { return true; }

bool RtDbManager::loadYxConfigsFromDb() { return true; }

bool RtDbManager::loadYkConfigsFromDb() { return true; }

bool RtDbManager::loadYtConfigsFromDb() { return true; }

bool RtDbManager::loadAllConfigs() {
    if (!configCache_) {
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
    dataChangeCallback_ = callback;
}

bool RtDbManager::updateYc(const std::string& code, const YcPoint& data) {
    if (!rtdb_) {
        return false;
    }

    bool result = rtdb_->setYc(code, data);

    if (result && dataChangeCallback_) {
        dataChangeCallback_("yc", code, &data);
    }

    return result;
}

bool RtDbManager::updateYx(const std::string& code, const YxPoint& data) {
    if (!rtdb_) {
        return false;
    }

    bool result = rtdb_->setYx(code, data);

    if (result && dataChangeCallback_) {
        dataChangeCallback_("yx", code, &data);
    }

    return result;
}

bool RtDbManager::updateYk(const std::string& code, const YkPoint& data) {
    if (!rtdb_) {
        return false;
    }

    bool result = rtdb_->setYk(code, data);

    if (result && dataChangeCallback_) {
        dataChangeCallback_("yk", code, &data);
    }

    return result;
}

bool RtDbManager::updateYt(const std::string& code, const YtPoint& data) {
    if (!rtdb_) {
        return false;
    }

    bool result = rtdb_->setYt(code, data);

    if (result && dataChangeCallback_) {
        dataChangeCallback_("yt", code, &data);
    }

    return result;
}

bool RtDbManager::batchUpdateYc(const std::map<std::string, YcPoint>& dataMap) {
    if (!rtdb_) {
        return false;
    }

    bool result = rtdb_->batchSetYc(dataMap);

    if (result && dataChangeCallback_) {
        for (const auto& pair : dataMap) {
            dataChangeCallback_("yc", pair.first, &pair.second);
        }
    }

    return result;
}

bool RtDbManager::batchUpdateYx(const std::map<std::string, YxPoint>& dataMap) {
    if (!rtdb_) {
        return false;
    }

    bool result = rtdb_->batchSetYx(dataMap);

    if (result && dataChangeCallback_) {
        for (const auto& pair : dataMap) {
            dataChangeCallback_("yx", pair.first, &pair.second);
        }
    }

    return result;
}

bool RtDbManager::batchUpdateYk(const std::map<std::string, YkPoint>& dataMap) {
    if (!rtdb_) {
        return false;
    }

    bool result = rtdb_->batchSetYk(dataMap);

    if (result && dataChangeCallback_) {
        for (const auto& pair : dataMap) {
            dataChangeCallback_("yk", pair.first, &pair.second);
        }
    }

    return result;
}

bool RtDbManager::batchUpdateYt(const std::map<std::string, YtPoint>& dataMap) {
    if (!rtdb_) {
        return false;
    }

    bool result = rtdb_->batchSetYt(dataMap);

    if (result && dataChangeCallback_) {
        for (const auto& pair : dataMap) {
            dataChangeCallback_("yt", pair.first, &pair.second);
        }
    }

    return result;
}

std::future<bool> RtDbManager::asyncUpdateYc(const std::string& code, const YcPoint& data) {
    return std::async(std::launch::async, [this, code, data]() { return updateYc(code, data); });
}

std::future<bool> RtDbManager::asyncUpdateYx(const std::string& code, const YxPoint& data) {
    return std::async(std::launch::async, [this, code, data]() { return updateYx(code, data); });
}

std::future<bool> RtDbManager::asyncUpdateYk(const std::string& code, const YkPoint& data) {
    return std::async(std::launch::async, [this, code, data]() { return updateYk(code, data); });
}

std::future<bool> RtDbManager::asyncUpdateYt(const std::string& code, const YtPoint& data) {
    return std::async(std::launch::async, [this, code, data]() { return updateYt(code, data); });
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

bool RtDbManager::getYcFull(const std::string& code, RtDbPointManager::YcFullPoint& fullPoint) {
    YcPoint data;
    if (getYc(code, data)) {
        return pointManager_->getYcFullPoint(code, data, fullPoint);
    }
    return false;
}

bool RtDbManager::getYxFull(const std::string& code, RtDbPointManager::YxFullPoint& fullPoint) {
    YxPoint data;
    if (getYx(code, data)) {
        return pointManager_->getYxFullPoint(code, data, fullPoint);
    }
    return false;
}

bool RtDbManager::getYkFull(const std::string& code, RtDbPointManager::YkFullPoint& fullPoint) {
    YkPoint data;
    if (getYk(code, data)) {
        return pointManager_->getYkFullPoint(code, data, fullPoint);
    }
    return false;
}

bool RtDbManager::getYtFull(const std::string& code, RtDbPointManager::YtFullPoint& fullPoint) {
    YtPoint data;
    if (getYt(code, data)) {
        return pointManager_->getYtFullPoint(code, data, fullPoint);
    }
    return false;
}

std::vector<YcPoint> RtDbManager::getAllYcData() {
    if (!rtdb_) {
        return {};
    }
    return rtdb_->getYcPoints("*");
}

std::vector<YxPoint> RtDbManager::getAllYxData() {
    if (!rtdb_) {
        return {};
    }
    return rtdb_->getYxPoints("*");
}

std::vector<YkPoint> RtDbManager::getAllYkData() {
    if (!rtdb_) {
        return {};
    }
    return rtdb_->getYkPoints("*");
}

std::vector<YtPoint> RtDbManager::getAllYtData() {
    if (!rtdb_) {
        return {};
    }
    return rtdb_->getYtPoints("*");
}

std::vector<RtDbPointManager::YcFullPoint> RtDbManager::getAllYcFullPoints() {
    std::vector<RtDbPointManager::YcFullPoint> result;

    if (!rtdb_ || !pointManager_ || !configCache_) {
        return result;
    }

    auto codes = configCache_->getYcConfigCodes();
    for (const auto& code : codes) {
        RtDbPointManager::YcFullPoint fullPoint;
        if (getYcFull(code, fullPoint)) {
            result.push_back(fullPoint);
        }
    }

    return result;
}

std::vector<RtDbPointManager::YxFullPoint> RtDbManager::getAllYxFullPoints() {
    std::vector<RtDbPointManager::YxFullPoint> result;

    if (!rtdb_ || !pointManager_ || !configCache_) {
        return result;
    }

    auto codes = configCache_->getYxConfigCodes();
    for (const auto& code : codes) {
        RtDbPointManager::YxFullPoint fullPoint;
        if (getYxFull(code, fullPoint)) {
            result.push_back(fullPoint);
        }
    }

    return result;
}

std::vector<RtDbPointManager::YkFullPoint> RtDbManager::getAllYkFullPoints() {
    std::vector<RtDbPointManager::YkFullPoint> result;

    if (!rtdb_ || !pointManager_ || !configCache_) {
        return result;
    }

    auto codes = configCache_->getYkConfigCodes();
    for (const auto& code : codes) {
        RtDbPointManager::YkFullPoint fullPoint;
        if (getYkFull(code, fullPoint)) {
            result.push_back(fullPoint);
        }
    }

    return result;
}

std::vector<RtDbPointManager::YtFullPoint> RtDbManager::getAllYtFullPoints() {
    std::vector<RtDbPointManager::YtFullPoint> result;

    if (!rtdb_ || !pointManager_ || !configCache_) {
        return result;
    }

    auto codes = configCache_->getYtConfigCodes();
    for (const auto& code : codes) {
        RtDbPointManager::YtFullPoint fullPoint;
        if (getYtFull(code, fullPoint)) {
            result.push_back(fullPoint);
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

    auto ycCodes = rtdb_->getYcCodes("*");
    for (const auto& code : ycCodes) {
        rtdb_->deleteYc(code);
    }

    auto yxCodes = rtdb_->getYxCodes("*");
    for (const auto& code : yxCodes) {
        rtdb_->deleteYx(code);
    }

    auto ykCodes = rtdb_->getYkCodes("*");
    for (const auto& code : ykCodes) {
        rtdb_->deleteYk(code);
    }

    auto ytCodes = rtdb_->getYtCodes("*");
    for (const auto& code : ytCodes) {
        rtdb_->deleteYt(code);
    }
}

bool RtDbManager::loadConfigFromJson(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    std::vector<RtDbConfigCache::YcConfig> ycConfigs;
    std::vector<RtDbConfigCache::YxConfig> yxConfigs;
    std::vector<RtDbConfigCache::YkConfig> ykConfigs;
    std::vector<RtDbConfigCache::YtConfig> ytConfigs;

    auto parseArray = [&](const std::string& key, auto& configs, auto parser) {
        auto pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return;
        pos = content.find("[", pos);
        if (pos == std::string::npos) return;
        pos++;
        while (pos < content.size()) {
            auto start = content.find("{", pos);
            auto end = content.find("}", start);
            if (start == std::string::npos || end == std::string::npos) break;
            std::string item = content.substr(start, end - start + 1);
            configs.push_back(parser(item));
            pos = end + 1;
        }
    };

    auto parseYcItem = [](const std::string& item) {
        RtDbConfigCache::YcConfig cfg;
        auto find = [&](const std::string& k) -> std::string {
            auto p = item.find("\"" + k + "\"");
            if (p == std::string::npos) return "";
            p = item.find(":", p + k.size() + 2);
            if (p == std::string::npos) return "";
            p++;
            while (p < item.size() && (item[p] == ' ' || item[p] == '\t')) p++;
            if (p >= item.size()) return "";
            if (item[p] == '"') {
                p++;
                std::string v;
                while (p < item.size() && item[p] != '"') {
                    if (item[p] == '\\' && p + 1 < item.size()) {
                        p++;
                        v += item[p];
                    } else {
                        v += item[p];
                    }
                    p++;
                }
                return v;
            }
            size_t e = p;
            while (e < item.size() && item[e] != ',' && item[e] != '}' && item[e] != ' ') e++;
            return item.substr(p, e - p);
        };
        cfg.code = find("code");
        cfg.name = find("name");
        try {
            cfg.channel_id = std::stoll(find("channel_id"));
        } catch (...) {
        }
        try {
            cfg.mulCoe = std::stod(find("mul_coe"));
        } catch (...) {
        }
        try {
            cfg.addCoe = std::stod(find("add_coe"));
        } catch (...) {
        }
        try {
            cfg.maxLimit = std::stod(find("max_limit"));
        } catch (...) {
        }
        try {
            cfg.minLimit = std::stod(find("min_limit"));
        } catch (...) {
        }
        cfg.unit = find("unit");
        try {
            cfg.status = std::stoi(find("status"));
        } catch (...) {
        }
        return cfg;
    };

    parseArray("yc_points", ycConfigs, parseYcItem);

    if (!ycConfigs.empty()) {
        configCache_->loadYcConfigs(ycConfigs);
    }
    if (!yxConfigs.empty()) {
        configCache_->loadYxConfigs(yxConfigs);
    }
    if (!ykConfigs.empty()) {
        configCache_->loadYkConfigs(ykConfigs);
    }
    if (!ytConfigs.empty()) {
        configCache_->loadYtConfigs(ytConfigs);
    }

    return true;
}

bool RtDbManager::loadConfigFromCsv(const std::string& filepath) { return false; }

std::unique_ptr<RtDbManager> createRtdbManager(const RtDbConfig& config) {
    auto manager = std::make_unique<RtDbManager>();
    if (manager->initialize(config)) {
        return manager;
    }
    return nullptr;
}

}  // namespace rtdb
}  // namespace ems