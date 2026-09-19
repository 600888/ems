/**
 * @file rtdb_ems.h
 * @brief EMS工商储实时数据库核心接口
 */
#ifndef EMS_RTDB_EMS_H
#define EMS_RTDB_EMS_H

#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "rtdb.h"

namespace ems {
namespace rtdb {

struct DeviceSnapshot {
    std::string device_id;
    std::string DeviceType;
    std::map<std::string, double> ycValues;
    std::map<std::string, int> yxValues;
    std::map<std::string, std::string> metadata;
    uint64_t timestamp{0};
    int quality{0};
};

struct DeviceCommand {
    std::string commandId;
    std::string device_id;
    std::string commandType;
    std::string pointCode;
    double value{0.0};
    int priority{0};
    uint64_t timestamp{0};
    std::string source;
};

struct CommandResult {
    std::string commandId;
    bool success{false};
    std::string message;
    uint64_t timestamp{0};
    int errorCode{0};
};

struct SystemTopology {
    std::string topologyId;
    std::string name;
    std::vector<std::string> deviceIds;
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

    bool updateDevicePoint(const std::string& device_id, const std::string& pointCode,
                           double value);
    bool updateDevicePoint(const std::string& device_id, const std::string& pointCode, int value);
    bool getDevicePoint(const std::string& device_id, const std::string& pointCode, double& value);
    bool getDevicePoint(const std::string& device_id, const std::string& pointCode, int& value);

    bool appendTimeSeriesData(const std::string& device_id, const std::string& pointCode,
                              double value, uint64_t timestamp = 0);
    std::vector<std::pair<uint64_t, double>> getTimeSeriesData(const std::string& device_id,
                                                               const std::string& pointCode,
                                                               uint64_t startTime = 0,
                                                               uint64_t endTime = 0,
                                                               int limit = 1000);
    bool cleanTimeSeriesData(const std::string& device_id, const std::string& pointCode,
                             uint64_t beforeTime = 0);

    bool pushCommand(const DeviceCommand& command);
    bool popCommand(const std::string& device_id, DeviceCommand& command, int timeoutMs = 0);
    bool ackCommand(const std::string& commandId, bool success, const std::string& message = "");
    bool getCommandResult(const std::string& commandId, CommandResult& result);

    bool acquireLock(const std::string& device_id, const std::string& ownerId,
                     int expireSeconds = 10);
    bool releaseLock(const std::string& device_id, const std::string& ownerId);
    bool isLocked(const std::string& device_id);
    std::string getLockOwner(const std::string& device_id);

    bool setTopology(const SystemTopology& topology);
    bool getTopology(const std::string& topologyId, SystemTopology& topology);
    bool deleteTopology(const std::string& topologyId);
    bool getAllTopologies(std::vector<SystemTopology>& topologies);

    bool acquireControlLock(const std::string& device_id, const std::string& ownerId,
                            int expireSeconds = 10);
    bool releaseControlLock(const std::string& device_id, const std::string& ownerId);
    bool executeWithLock(const std::string& device_id, const std::string& ownerId,
                         std::function<bool()> operation, int timeoutMs = 5000);

    bool isConnected() const { return rtdb_ && rtdb_->isConnected(); }
    std::string getLastError() const { return rtdb_ ? rtdb_->getLastError() : "Not initialized"; }

   private:
    std::string buildDeviceSnapshotKey(const std::string& device_id) const;
    std::string buildDevicePointKey(const std::string& device_id,
                                    const std::string& pointCode) const;
    std::string buildTimeSeriesKey(const std::string& device_id,
                                   const std::string& pointCode) const;
    std::string buildCommandQueueKey(const std::string& device_id) const;
    std::string buildCommandResultKey(const std::string& commandId) const;
    std::string buildDeviceLockKey(const std::string& device_id) const;
    std::string buildControlLockKey(const std::string& device_id) const;
    std::string buildTopologyKey(const std::string& topologyId) const;
    std::string buildTopologyIndexKey() const;

    bool checkSocForDischarge(const std::string& device_id, double targetSoc);
    bool executeControlWithSocCheck(const std::string& device_id, const std::string& pointCode,
                                    double value, const std::string& ownerId);

    std::unique_ptr<RtDb> rtdb_;
    RtDbConfig config_;
    bool initialized_{false};
};

std::unique_ptr<RTDBEMS> createRtdbEms(const RtDbConfig& config);

}  // namespace rtdb
}  // namespace ems

#endif