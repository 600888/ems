#ifndef EMS_RTDB_MANAGER_H
#define EMS_RTDB_MANAGER_H

#include <functional>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "rtdb.h"
#include "rtdb_cache.h"

namespace ems {
namespace rtdb {

class RtDbManager {
   public:
    RtDbManager();
    ~RtDbManager();

    bool initialize(const RtDbConfig& config);
    void shutdown();

    std::shared_ptr<RtDb> getRtDb() const { return rtdb_; }
    std::shared_ptr<RtDbConfigCache> getConfigCache() const { return configCache_; }
    std::shared_ptr<RtDbPointManager> getPointManager() const { return pointManager_; }

    bool loadYcConfigsFromDb();
    bool loadYxConfigsFromDb();
    bool loadYkConfigsFromDb();
    bool loadYtConfigsFromDb();
    bool loadAllConfigs();

    using DataChangeCallback = std::function<void(const std::string& pointType,
                                                  const std::string& pointCode, const void* data)>;

    void setDataChangeCallback(DataChangeCallback callback);

    bool updateYc(const std::string& code, const YcPoint& data);
    bool updateYx(const std::string& code, const YxPoint& data);
    bool updateYk(const std::string& code, const YkPoint& data);
    bool updateYt(const std::string& code, const YtPoint& data);

    bool batchUpdateYc(const std::map<std::string, YcPoint>& dataMap);
    bool batchUpdateYx(const std::map<std::string, YxPoint>& dataMap);
    bool batchUpdateYk(const std::map<std::string, YkPoint>& dataMap);
    bool batchUpdateYt(const std::map<std::string, YtPoint>& dataMap);

    std::future<bool> asyncUpdateYc(const std::string& code, const YcPoint& data);
    std::future<bool> asyncUpdateYx(const std::string& code, const YxPoint& data);
    std::future<bool> asyncUpdateYk(const std::string& code, const YkPoint& data);
    std::future<bool> asyncUpdateYt(const std::string& code, const YtPoint& data);

    bool getYc(const std::string& code, YcPoint& data);
    bool getYx(const std::string& code, YxPoint& data);
    bool getYk(const std::string& code, YkPoint& data);
    bool getYt(const std::string& code, YtPoint& data);

    bool getYcFull(const std::string& code, RtDbPointManager::YcFullPoint& fullPoint);
    bool getYxFull(const std::string& code, RtDbPointManager::YxFullPoint& fullPoint);
    bool getYkFull(const std::string& code, RtDbPointManager::YkFullPoint& fullPoint);
    bool getYtFull(const std::string& code, RtDbPointManager::YtFullPoint& fullPoint);

    std::vector<YcPoint> getAllYcData();
    std::vector<YxPoint> getAllYxData();
    std::vector<YkPoint> getAllYkData();
    std::vector<YtPoint> getAllYtData();

    std::vector<RtDbPointManager::YcFullPoint> getAllYcFullPoints();
    std::vector<RtDbPointManager::YxFullPoint> getAllYxFullPoints();
    std::vector<RtDbPointManager::YkFullPoint> getAllYkFullPoints();
    std::vector<RtDbPointManager::YtFullPoint> getAllYtFullPoints();

    size_t getYcDataCount();
    size_t getYxDataCount();
    size_t getYkDataCount();
    size_t getYtDataCount();

    bool isConnected() const { return rtdb_ && rtdb_->isConnected(); }
    std::string getLastError() const {
        return rtdb_ ? rtdb_->getLastError() : "RTDB not initialized";
    }

    void clearAllData();

    bool loadConfigFromJson(const std::string& filepath);
    bool loadConfigFromCsv(const std::string& filepath);

   private:
    std::shared_ptr<RtDb> rtdb_;
    std::shared_ptr<RtDbConfigCache> configCache_;
    std::shared_ptr<RtDbPointManager> pointManager_;
    DataChangeCallback dataChangeCallback_;
};

std::unique_ptr<RtDbManager> createRtdbManager(const RtDbConfig& config);

}  // namespace rtdb
}  // namespace ems

#endif