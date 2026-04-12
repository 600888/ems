#ifndef EMS_RTDB_MANAGER_H
#define EMS_RTDB_MANAGER_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include "rtdb.h"
#include "rtdb_cache.h"

namespace ems {
namespace net {

class RtDbManager {
public:
    RtDbManager();
    ~RtDbManager();

    bool initialize(const RtDbConfig& config);
    void shutdown();

    std::shared_ptr<RtDb> getRtDb() const { return rtdb_; }
    std::shared_ptr<RtDbConfigCache> getConfigCache() const { return config_cache_; }
    std::shared_ptr<RtDbPointManager> getPointManager() const { return point_manager_; }

    bool loadYcConfigsFromDb();
    bool loadYxConfigsFromDb();
    bool loadYkConfigsFromDb();
    bool loadYtConfigsFromDb();
    bool loadAllConfigs();

    using DataChangeCallback = std::function<void(
        const std::string& point_type,
        const std::string& point_code,
        const void* data
    )>;

    void setDataChangeCallback(DataChangeCallback callback);

    bool updateYc(const std::string& code, const YcPoint& data);
    bool updateYx(const std::string& code, const YxPoint& data);
    bool updateYk(const std::string& code, const YkPoint& data);
    bool updateYt(const std::string& code, const YtPoint& data);

    bool batchUpdateYC(const std::map<std::string, YcPoint>& data_map);
    bool batchUpdateYX(const std::map<std::string, YxPoint>& data_map);
    bool batchUpdateYK(const std::map<std::string, YkPoint>& data_map);
    bool batchUpdateYT(const std::map<std::string, YtPoint>& data_map);

    bool getYc(const std::string& code, YcPoint& data);
    bool getYx(const std::string& code, YxPoint& data);
    bool getYk(const std::string& code, YkPoint& data);
    bool getYt(const std::string& code, YtPoint& data);

    bool getYcFull(const std::string& code, 
                   RtDbPointManager::YCFullPoint& fullPoint);
    bool getYxFull(const std::string& code, 
                   RtDbPointManager::YXFullPoint& fullPoint);
    bool getYkFull(const std::string& code, 
                   RtDbPointManager::YKFullPoint& fullPoint);
    bool getYtFull(const std::string& code, 
                   RtDbPointManager::YTFullPoint& fullPoint);

    std::vector<YcPoint> getAllYcData();
    std::vector<YxPoint> getAllYxData();
    std::vector<YkPoint> getAllYkData();
    std::vector<YtPoint> getAllYtData();

    std::vector<RtDbPointManager::YCFullPoint> getAllYcFullPoints();
    std::vector<RtDbPointManager::YXFullPoint> getAllYxFullPoints();
    std::vector<RtDbPointManager::YKFullPoint> getAllYkFullPoints();
    std::vector<RtDbPointManager::YTFullPoint> getAllYtFullPoints();

    size_t getYcDataCount();
    size_t getYxDataCount();
    size_t getYkDataCount();
    size_t getYtDataCount();

    bool isConnected() const { return rtdb_ && rtdb_->isConnected(); }
    std::string getLastError() const { 
        return rtdb_ ? rtdb_->getLastError() : "RTDB not initialized"; 
    }

    void clearAllData();

private:
    std::shared_ptr<RtDb> rtdb_;
    std::shared_ptr<RtDbConfigCache> config_cache_;
    std::shared_ptr<RtDbPointManager> point_manager_;
    DataChangeCallback data_change_callback_;
};

std::unique_ptr<RtDbManager> createRTDBManager(const RtDbConfig& config);

}
}

#endif
