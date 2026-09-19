/**
 * @file rtdb_ems.cpp
 * @brief EMS工商储实时数据库实现
 */
#include "rtdb_ems.h"

#include <sw/redis++/redis++.h>

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

namespace ems {
namespace rtdb {

static std::string generateUUID() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::uniform_int_distribution<> dis2(8, 11);

    std::ostringstream ss;
    ss << std::hex;
    ss << std::setfill('0');

    for (int i = 0; i < 8; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 4; i++) ss << dis(gen);
    ss << "-";
    ss << "4";
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    ss << dis2(gen);
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 12; i++) ss << dis(gen);

    return ss.str();
}

static uint64_t currentTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

RTDBEMS::RTDBEMS() : initialized_(false) {}

RTDBEMS::~RTDBEMS() { shutdown(); }

bool RTDBEMS::initialize(const RtDbConfig& config) {
    config_ = config;
    if (config_.prefix.empty()) {
        config_.prefix = "ems:";
    }

    rtdb_ = createRtDb(config_);
    if (!rtdb_ || !rtdb_->isConnected()) {
        return false;
    }

    initialized_ = true;
    return true;
}

void RTDBEMS::shutdown() {
    if (rtdb_) {
        rtdb_->shutdown();
        rtdb_.reset();
    }
    initialized_ = false;
}

std::string RTDBEMS::buildDeviceSnapshotKey(const std::string& device_id) const {
    return "device:" + device_id + ":rtdata";
}

std::string RTDBEMS::buildDevicePointKey(const std::string& device_id,
                                         const std::string& pointCode) const {
    return "device:" + device_id + ":points:" + pointCode;
}

std::string RTDBEMS::buildTimeSeriesKey(const std::string& device_id,
                                        const std::string& pointCode) const {
    return "device:" + device_id + ":history:" + pointCode;
}

std::string RTDBEMS::buildCommandQueueKey(const std::string& device_id) const {
    return "cmd:" + device_id + ":queue";
}

std::string RTDBEMS::buildCommandResultKey(const std::string& commandId) const {
    return "cmd:result:" + commandId;
}

std::string RTDBEMS::buildDeviceLockKey(const std::string& device_id) const {
    return "lock:device:" + device_id;
}

std::string RTDBEMS::buildControlLockKey(const std::string& device_id) const {
    return "lock:ctrl:" + device_id;
}

std::string RTDBEMS::buildTopologyKey(const std::string& topologyId) const {
    return "config:topology:" + topologyId;
}

std::string RTDBEMS::buildTopologyIndexKey() const { return "config:topology:index"; }

bool RTDBEMS::setDeviceSnapshot(const std::string& device_id, const DeviceSnapshot& snapshot) {
    if (!rtdb_) return false;

    try {
        std::string key = buildDeviceSnapshotKey(device_id);

        std::map<std::string, std::string> fields;
        fields["device_id"] = device_id;
        fields["device_type"] = snapshot.DeviceType;
        fields["timestamp"] =
            std::to_string(snapshot.timestamp > 0 ? snapshot.timestamp : currentTimestamp());
        fields["quality"] = std::to_string(snapshot.quality);

        for (const auto& yc : snapshot.ycValues) {
            fields["yc:" + yc.first] = std::to_string(yc.second);
        }
        for (const auto& yx : snapshot.yxValues) {
            fields["yx:" + yx.first] = std::to_string(yx.second);
        }
        for (const auto& meta : snapshot.metadata) {
            fields["meta:" + meta.first] = meta.second;
        }

        if (!rtdb_->setHash(key, fields)) {
            return false;
        }

        rtdb_->expire(key, 30);

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool RTDBEMS::getDeviceSnapshot(const std::string& device_id, DeviceSnapshot& snapshot) {
    if (!rtdb_) return false;

    try {
        std::string key = buildDeviceSnapshotKey(device_id);
        std::map<std::string, std::string> fields;

        if (!rtdb_->getHash(key, fields)) {
            return false;
        }

        snapshot.device_id = device_id;
        snapshot.ycValues.clear();
        snapshot.yxValues.clear();
        snapshot.metadata.clear();

        for (const auto& field : fields) {
            if (field.first == "device_id") {
                snapshot.device_id = field.second;
            } else if (field.first == "device_type") {
                snapshot.DeviceType = field.second;
            } else if (field.first == "timestamp") {
                snapshot.timestamp = std::stoull(field.second);
            } else if (field.first == "quality") {
                snapshot.quality = std::stoi(field.second);
            } else if (field.first.rfind("yc:", 0) == 0) {
                snapshot.ycValues[field.first.substr(3)] = std::stod(field.second);
            } else if (field.first.rfind("yx:", 0) == 0) {
                snapshot.yxValues[field.first.substr(3)] = std::stoi(field.second);
            } else if (field.first.rfind("meta:", 0) == 0) {
                snapshot.metadata[field.first.substr(5)] = field.second;
            }
        }

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool RTDBEMS::deleteDeviceSnapshot(const std::string& device_id) {
    if (!rtdb_) return false;
    return rtdb_->deleteKey(buildDeviceSnapshotKey(device_id));
}

bool RTDBEMS::updateDevicePoint(const std::string& device_id, const std::string& pointCode,
                                double value) {
    if (!rtdb_) return false;

    try {
        std::string key = buildDeviceSnapshotKey(device_id);
        std::string field = "yc:" + pointCode;
        std::string valueStr = std::to_string(value);

        if (!rtdb_->setHashField(key, field, valueStr)) {
            return false;
        }

        rtdb_->expire(key, 30);

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool RTDBEMS::updateDevicePoint(const std::string& device_id, const std::string& pointCode,
                                int value) {
    if (!rtdb_) return false;

    try {
        std::string key = buildDeviceSnapshotKey(device_id);
        std::string field = "yx:" + pointCode;
        std::string valueStr = std::to_string(value);

        if (!rtdb_->setHashField(key, field, valueStr)) {
            return false;
        }

        rtdb_->expire(key, 30);

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool RTDBEMS::getDevicePoint(const std::string& device_id, const std::string& pointCode,
                             double& value) {
    if (!rtdb_) return false;

    try {
        std::string key = buildDeviceSnapshotKey(device_id);
        std::string field = "yc:" + pointCode;
        std::string valueStr;
        std::map<std::string, std::string> fields = {{field, valueStr}};

        if (rtdb_->getHash(key, fields)) {
            value = std::stod(fields[field]);
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        return false;
    }
}

bool RTDBEMS::getDevicePoint(const std::string& device_id, const std::string& pointCode,
                             int& value) {
    if (!rtdb_) return false;

    try {
        std::string key = buildDeviceSnapshotKey(device_id);
        std::string field = "yx:" + pointCode;
        std::string valueStr;
        std::map<std::string, std::string> fields = {{field, valueStr}};

        if (rtdb_->getHash(key, fields)) {
            value = std::stoi(fields[field]);
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        return false;
    }
}

bool RTDBEMS::appendTimeSeriesData(const std::string& device_id, const std::string& pointCode,
                                   double value, uint64_t timestamp) {
    if (!rtdb_) return false;

    try {
        std::string key = buildTimeSeriesKey(device_id, pointCode);
        uint64_t ts = timestamp > 0 ? timestamp : currentTimestamp();

        std::map<std::string, std::string> members;
        members[std::to_string(ts)] = std::to_string(value);

        return rtdb_->setHash(key, members);
    } catch (const std::exception& e) {
        return false;
    }
}

std::vector<std::pair<uint64_t, double>> RTDBEMS::getTimeSeriesData(const std::string& device_id,
                                                                    const std::string& pointCode,
                                                                    uint64_t startTime,
                                                                    uint64_t endTime, int limit) {
    std::vector<std::pair<uint64_t, double>> result;
    if (!rtdb_) return result;

    try {
        std::string key = buildTimeSeriesKey(device_id, pointCode);
        std::map<std::string, std::string> fields;

        if (!rtdb_->getHash(key, fields)) {
            return result;
        }

        for (const auto& field : fields) {
            uint64_t ts = std::stoull(field.first);

            if (startTime > 0 && ts < startTime) continue;
            if (endTime > 0 && ts > endTime) continue;

            result.emplace_back(ts, std::stod(field.second));
        }

        std::sort(result.begin(), result.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        if (limit > 0 && static_cast<int>(result.size()) > limit) {
            result.resize(limit);
        }

        return result;
    } catch (const std::exception& e) {
        return result;
    }
}

bool RTDBEMS::cleanTimeSeriesData(const std::string& device_id, const std::string& pointCode,
                                  uint64_t beforeTime) {
    if (!rtdb_) return false;

    if (beforeTime == 0) {
        return rtdb_->deleteKey(buildTimeSeriesKey(device_id, pointCode));
    }

    return true;
}

bool RTDBEMS::pushCommand(const DeviceCommand& command) {
    if (!rtdb_) return false;

    try {
        std::string key = buildCommandQueueKey(command.device_id);

        std::map<std::string, std::string> cmdData;
        cmdData["command_id"] = command.commandId.empty() ? generateUUID() : command.commandId;
        cmdData["device_id"] = command.device_id;
        cmdData["command_type"] = command.commandType;
        cmdData["point_code"] = command.pointCode;
        cmdData["value"] = std::to_string(command.value);
        cmdData["priority"] = std::to_string(command.priority);
        cmdData["timestamp"] =
            std::to_string(command.timestamp > 0 ? command.timestamp : currentTimestamp());
        cmdData["source"] = command.source;
        cmdData["status"] = "pending";

        std::string cmdJson;
        for (const auto& d : cmdData) {
            cmdJson += d.first + ":" + d.second + ";";
        }

        return rtdb_->append(key, cmdJson);
    } catch (const std::exception& e) {
        return false;
    }
}

bool RTDBEMS::popCommand(const std::string& device_id, DeviceCommand& command, int timeoutMs) {
    if (!rtdb_) return false;

    try {
        std::string key = buildCommandQueueKey(device_id);
        std::string cmdJson;

        if (!rtdb_->get(key, cmdJson)) {
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool RTDBEMS::ackCommand(const std::string& commandId, bool success, const std::string& message) {
    if (!rtdb_) return false;

    try {
        std::string key = buildCommandResultKey(commandId);

        std::map<std::string, std::string> result;
        result["command_id"] = commandId;
        result["success"] = success ? "true" : "false";
        result["message"] = message;
        result["timestamp"] = std::to_string(currentTimestamp());

        if (!rtdb_->setHash(key, result)) {
            return false;
        }

        rtdb_->expire(key, 3600);

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool RTDBEMS::getCommandResult(const std::string& commandId, CommandResult& result) {
    if (!rtdb_) return false;

    try {
        std::string key = buildCommandResultKey(commandId);
        std::map<std::string, std::string> fields;

        if (!rtdb_->getHash(key, fields)) {
            return false;
        }

        result.commandId = commandId;
        result.success = fields["success"] == "true";
        result.message = fields["message"];
        result.timestamp = std::stoull(fields["timestamp"]);

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool RTDBEMS::acquireLock(const std::string& device_id, const std::string& ownerId,
                          int expireSeconds) {
    if (!rtdb_) return false;

    try {
        std::string key = buildDeviceLockKey(device_id);
        std::string value = ownerId + ":" + std::to_string(currentTimestamp());

        return rtdb_->setWithExpire(key, value, expireSeconds);
    } catch (const std::exception& e) {
        return false;
    }
}

bool RTDBEMS::releaseLock(const std::string& device_id, const std::string& ownerId) {
    if (!rtdb_) return false;

    try {
        std::string key = buildDeviceLockKey(device_id);
        std::string currentOwner;

        if (!rtdb_->get(key, currentOwner)) {
            return true;
        }

        if (currentOwner.find(ownerId) == 0) {
            return rtdb_->deleteKey(key);
        }

        return false;
    } catch (const std::exception& e) {
        return false;
    }
}

bool RTDBEMS::isLocked(const std::string& device_id) {
    if (!rtdb_) return false;
    return rtdb_->exists(buildDeviceLockKey(device_id));
}

std::string RTDBEMS::getLockOwner(const std::string& device_id) {
    if (!rtdb_) return "";

    try {
        std::string key = buildDeviceLockKey(device_id);
        std::string value;

        if (!rtdb_->get(key, value)) {
            return "";
        }

        size_t colonPos = value.find(':');
        if (colonPos != std::string::npos) {
            return value.substr(0, colonPos);
        }

        return value;
    } catch (const std::exception& e) {
        return "";
    }
}

bool RTDBEMS::setTopology(const SystemTopology& topology) {
    if (!rtdb_) return false;

    try {
        std::string key = buildTopologyKey(topology.topologyId);

        std::map<std::string, std::string> data;
        data["topology_id"] = topology.topologyId;
        data["name"] = topology.name;

        std::ostringstream devicesStream;
        for (size_t i = 0; i < topology.deviceIds.size(); ++i) {
            if (i > 0) devicesStream << ",";
            devicesStream << topology.deviceIds[i];
        }
        data["device_ids"] = devicesStream.str();

        for (const auto& rel : topology.relationships) {
            data["rel:" + rel.first] = rel.second;
        }
        for (const auto& meta : topology.metadata) {
            data["meta:" + meta.first] = meta.second;
        }

        if (!rtdb_->setHash(key, data)) {
            return false;
        }

        rtdb_->setHash(buildTopologyIndexKey(), {{topology.topologyId, "1"}});

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool RTDBEMS::getTopology(const std::string& topologyId, SystemTopology& topology) {
    if (!rtdb_) return false;

    try {
        std::string key = buildTopologyKey(topologyId);
        std::map<std::string, std::string> data;

        if (!rtdb_->getHash(key, data)) {
            return false;
        }

        topology.topologyId = topologyId;
        topology.name = data.count("name") ? data["name"] : "";
        topology.deviceIds.clear();
        topology.relationships.clear();
        topology.metadata.clear();

        std::string devicesStr = data.count("device_ids") ? data["device_ids"] : "";
        if (!devicesStr.empty()) {
            std::istringstream stream(devicesStr);
            std::string device_id;
            while (std::getline(stream, device_id, ',')) {
                if (!device_id.empty()) {
                    topology.deviceIds.push_back(device_id);
                }
            }
        }

        for (const auto& d : data) {
            if (d.first.rfind("rel:", 0) == 0) {
                topology.relationships[d.first.substr(4)] = d.second;
            } else if (d.first.rfind("meta:", 0) == 0) {
                topology.metadata[d.first.substr(5)] = d.second;
            }
        }

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool RTDBEMS::deleteTopology(const std::string& topologyId) {
    if (!rtdb_) return false;

    try {
        std::string key = buildTopologyKey(topologyId);

        if (!rtdb_->deleteKey(key)) {
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool RTDBEMS::getAllTopologies(std::vector<SystemTopology>& topologies) {
    if (!rtdb_) return false;

    try {
        std::map<std::string, std::string> index;
        if (!rtdb_->getHash(buildTopologyIndexKey(), index)) {
            return false;
        }

        for (const auto& item : index) {
            SystemTopology topology;
            if (getTopology(item.first, topology)) {
                topologies.push_back(topology);
            }
        }

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool RTDBEMS::acquireControlLock(const std::string& device_id, const std::string& ownerId,
                                 int expireSeconds) {
    if (!rtdb_) return false;

    try {
        std::string key = buildControlLockKey(device_id);
        std::string value = ownerId + ":" + std::to_string(currentTimestamp());

        return rtdb_->setWithExpire(key, value, expireSeconds);
    } catch (const std::exception& e) {
        return false;
    }
}

bool RTDBEMS::releaseControlLock(const std::string& device_id, const std::string& ownerId) {
    if (!rtdb_) return false;

    try {
        std::string key = buildControlLockKey(device_id);
        std::string currentOwner;

        if (!rtdb_->get(key, currentOwner)) {
            return true;
        }

        if (currentOwner.find(ownerId) == 0) {
            return rtdb_->deleteKey(key);
        }

        return false;
    } catch (const std::exception& e) {
        return false;
    }
}

bool RTDBEMS::executeWithLock(const std::string& device_id, const std::string& ownerId,
                              std::function<bool()> operation, int timeoutMs) {
    if (!acquireControlLock(device_id, ownerId, timeoutMs / 1000)) {
        return false;
    }

    try {
        bool result = operation();
        releaseControlLock(device_id, ownerId);
        return result;
    } catch (const std::exception& e) {
        releaseControlLock(device_id, ownerId);
        return false;
    }
}

std::unique_ptr<RTDBEMS> createRtdbEms(const RtDbConfig& config) {
    auto rtdbEms = std::make_unique<RTDBEMS>();
    if (!rtdbEms->initialize(config)) {
        return nullptr;
    }
    return rtdbEms;
}

}  // namespace rtdb
}  // namespace ems