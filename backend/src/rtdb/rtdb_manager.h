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

class RTDBManager {
public:
    RTDBManager();
    ~RTDBManager();

    bool initialize(const RTDBConfig& config);
    void shutdown();

    std::shared_ptr<RTDB> getRTDB() const { return rtdb_; }
    std::shared_ptr<RTDBConfigCache> getConfigCache() const { return config_cache_; }
    std::shared_ptr<RTDBPointManager> getPointManager() const { return point_manager_; }

    bool loadYCConfigsFromDB();
    bool loadYXConfigsFromDB();
    bool loadYKConfigsFromDB();
    bool loadYTConfigsFromDB();
    bool loadAllConfigs();

    using DataChangeCallback = std::function<void(
        const std::string& point_type,
        const std::string& point_code,
        const void* data
    )>;

    void setDataChangeCallback(DataChangeCallback callback);

    bool updateYC(const std::string& code, const YCPoint& data);
    bool updateYX(const std::string& code, const YXPoint& data);
    bool updateYK(const std::string& code, const YKPoint& data);
    bool updateYT(const std::string& code, const YTPoint& data);

    bool batchUpdateYC(const std::map<std::string, YCPoint>& data_map);
    bool batchUpdateYX(const std::map<std::string, YXPoint>& data_map);
    bool batchUpdateYK(const std::map<std::string, YKPoint>& data_map);
    bool batchUpdateYT(const std::map<std::string, YTPoint>& data_map);

    bool getYC(const std::string& code, YCPoint& data);
    bool getYX(const std::string& code, YXPoint& data);
    bool getYK(const std::string& code, YKPoint& data);
    bool getYT(const std::string& code, YTPoint& data);

    bool getYCFull(const std::string& code, 
                   RTDBPointManager::YCFullPoint& full_point);
    bool getYXFull(const std::string& code, 
                   RTDBPointManager::YXFullPoint& full_point);
    bool getYKFull(const std::string& code, 
                   RTDBPointManager::YKFullPoint& full_point);
    bool getYTFull(const std::string& code, 
                   RTDBPointManager::YTFullPoint& full_point);

    std::vector<YCPoint> getAllYCData();
    std::vector<YXPoint> getAllYXData();
    std::vector<YKPoint> getAllYKData();
    std::vector<YTPoint> getAllYTData();

    std::vector<RTDBPointManager::YCFullPoint> getAllYCFullPoints();
    std::vector<RTDBPointManager::YXFullPoint> getAllYXFullPoints();
    std::vector<RTDBPointManager::YKFullPoint> getAllYKFullPoints();
    std::vector<RTDBPointManager::YTFullPoint> getAllYTFullPoints();

    size_t getYCDataCount();
    size_t getYXDataCount();
    size_t getYKDataCount();
    size_t getYTDataCount();

    bool isConnected() const { return rtdb_ && rtdb_->isConnected(); }
    std::string getLastError() const { 
        return rtdb_ ? rtdb_->getLastError() : "RTDB not initialized"; 
    }

    void clearAllData();

private:
    std::shared_ptr<RTDB> rtdb_;
    std::shared_ptr<RTDBConfigCache> config_cache_;
    std::shared_ptr<RTDBPointManager> point_manager_;
    DataChangeCallback data_change_callback_;
};

std::unique_ptr<RTDBManager> createRTDBManager(const RTDBConfig& config);

}
}

#endif
