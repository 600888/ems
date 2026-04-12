/**
 * @file rtdb_ems.h
 * @brief EMS工商储实时数据库核心接口
 */
#ifndef EMS_RTDB_EMS_H
#define EMS_RTDB_EMS_H

#include "rtdb.h"
#include <vector>
#include <map>
#include <string>
#include <functional>
#include <chrono>

namespace ems {
namespace net {

struct DeviceSnapshot {
    std::string device_id;
    std::string device_type;
    std::map<std::string, double> yc_values;
    std::map<std::string, int> yx_values;
    std::map<std::string, std::string> metadata;
    uint64_t timestamp{0};
    int quality{0};
};

struct DeviceCommand {
    std::string command_id;
    std::string device_id;
    std::string command_type;
    std::string point_code;
    double value{0.0};
    int priority{0};
    uint64_t timestamp{0};
    std::string source;
};

struct CommandResult {
    std::string command_id;
    bool success{false};
    std::string message;
    uint64_t timestamp{0};
    int error_code{0};
};

struct SystemTopology {
    std::string topology_id;
    std::string name;
    std::vector<std::string> device_ids;
    std::map<std::string, std::string> relationships;
    std::map<std::string, std::string> metadata;
};

class RTDBEMS {
public:
    RTDBEMS();
    ~RTDBEMS();

    bool initialize(const RtDbConfig& config);
    void shutdown();

    bool setDeviceSnapshot(const std::string& device_id, const DeviceSnapshot& snapshot);
    bool getDeviceSnapshot(const std::string& device_id, DeviceSnapshot& snapshot);
    bool deleteDeviceSnapshot(const std::string& device_id);
    
    bool updateDevicePoint(const std::string& device_id, const std::string& point_code, double value);
    bool updateDevicePoint(const std::string& device_id, const std::string& point_code, int value);
    bool getDevicePoint(const std::string& device_id, const std::string& point_code, double& value);
    bool getDevicePoint(const std::string& device_id, const std::string& point_code, int& value);
    
    bool appendTimeSeriesData(const std::string& device_id, const std::string& point_code, 
                              double value, uint64_t timestamp = 0);
    std::vector<std::pair<uint64_t, double>> getTimeSeriesData(const std::string& device_id,
                                                                const std::string& point_code,
                                                                uint64_t start_time = 0,
                                                                uint64_t end_time = 0,
                                                                int limit = 1000);
    bool cleanTimeSeriesData(const std::string& device_id, const std::string& point_code,
                             uint64_t before_time = 0);
    
    bool pushCommand(const DeviceCommand& command);
    bool popCommand(const std::string& device_id, DeviceCommand& command, int timeout_ms = 0);
    bool ackCommand(const std::string& command_id, bool success, const std::string& message = "");
    bool getCommandResult(const std::string& command_id, CommandResult& result);
    
    bool acquireLock(const std::string& device_id, const std::string& owner_id, int expire_seconds = 10);
    bool releaseLock(const std::string& device_id, const std::string& owner_id);
    bool isLocked(const std::string& device_id);
    std::string getLockOwner(const std::string& device_id);
    
    bool setTopology(const SystemTopology& topology);
    bool getTopology(const std::string& topology_id, SystemTopology& topology);
    bool deleteTopology(const std::string& topology_id);
    bool getAllTopologies(std::vector<SystemTopology>& topologies);
    
    bool acquireControlLock(const std::string& device_id, const std::string& owner_id, 
                           int expire_seconds = 10);
    bool releaseControlLock(const std::string& device_id, const std::string& owner_id);
    bool executeWithLock(const std::string& device_id, const std::string& owner_id,
                        std::function<bool()> operation, int timeout_ms = 5000);

    bool isConnected() const { return rtdb_ && rtdb_->isConnected(); }
    std::string getLastError() const { return rtdb_ ? rtdb_->getLastError() : "Not initialized"; }

private:
    std::string buildDeviceSnapshotKey(const std::string& device_id) const;
    std::string buildDevicePointKey(const std::string& device_id, const std::string& point_code) const;
    std::string buildTimeSeriesKey(const std::string& device_id, const std::string& point_code) const;
    std::string buildCommandQueueKey(const std::string& device_id) const;
    std::string buildCommandResultKey(const std::string& command_id) const;
    std::string buildDeviceLockKey(const std::string& device_id) const;
    std::string buildControlLockKey(const std::string& device_id) const;
    std::string buildTopologyKey(const std::string& topology_id) const;
    std::string buildTopologyIndexKey() const;
    
    bool checkSocForDischarge(const std::string& deviceId, double targetSoc);
    bool executeControlWithSocCheck(const std::string& deviceId, const std::string& pointCode,
                                   double value, const std::string& owner_id);

    std::unique_ptr<RtDb> rtdb_;
    RtDbConfig config_;
    bool initialized_{false};
};

std::unique_ptr<RTDBEMS> createRTDBEMS(const RtDbConfig& config);

}
}

#endif
