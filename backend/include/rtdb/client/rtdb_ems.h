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

#include "rtdb/client/rtdb.h"

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

class EMS_RTDB_API RTDBEMS {
   public:
    /** @brief 构造 RTDBEMS 实例。 */
    RTDBEMS();
    /** @brief 释放 RTDBEMS 持有的资源。 */
    ~RTDBEMS();

    /**
     * @brief 初始化连接和运行配置。
     * @param[in] config 配置数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool initialize(const RtDbConfig& config);
    /** @brief 停止服务并释放资源。 */
    void shutdown();

    /**
     * @brief 写入设备快照。
     * @param[in] device_id 设备 ID。
     * @param[in] snapshot 设备快照。
     * @return true 表示操作成功；false 表示失败。
     */
    bool setDeviceSnapshot(const std::string& device_id, const DeviceSnapshot& snapshot);
    /**
     * @brief 读取设备快照。
     * @param[in] device_id 设备 ID。
     * @param[out] snapshot 设备快照。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getDeviceSnapshot(const std::string& device_id, DeviceSnapshot& snapshot);
    /**
     * @brief 删除设备快照。
     * @param[in] device_id 设备 ID。
     * @return true 表示操作成功；false 表示失败。
     */
    bool deleteDeviceSnapshot(const std::string& device_id);

    /**
     * @brief 更新设备测点。
     * @param[in] device_id 设备 ID。
     * @param[in] pointCode 测点编码。
     * @param[in] value 待写入的值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool updateDevicePoint(const std::string& device_id, const std::string& pointCode,
                           double value);
    /**
     * @brief 更新设备测点。
     * @param[in] device_id 设备 ID。
     * @param[in] pointCode 测点编码。
     * @param[in] value 待写入的值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool updateDevicePoint(const std::string& device_id, const std::string& pointCode, int value);
    /**
     * @brief 读取设备测点。
     * @param[in] device_id 设备 ID。
     * @param[in] pointCode 测点编码。
     * @param[out] value 接收读取结果的值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getDevicePoint(const std::string& device_id, const std::string& pointCode, double& value);
    /**
     * @brief 读取设备测点。
     * @param[in] device_id 设备 ID。
     * @param[in] pointCode 测点编码。
     * @param[out] value 接收读取结果的值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getDevicePoint(const std::string& device_id, const std::string& pointCode, int& value);

    /**
     * @brief 向设备测点时间序列追加数据。
     * @param[in] device_id 设备 ID。
     * @param[in] pointCode 测点编码。
     * @param[in] value 输入值。
     * @param[in] timestamp 数据时间戳。
     * @return true 表示操作成功；false 表示失败。
     */
    bool appendTimeSeriesData(const std::string& device_id, const std::string& pointCode,
                              double value, uint64_t timestamp = 0);
    /**
     * @brief 读取指定时间范围内的测点序列。
     * @param[in] device_id 设备 ID。
     * @param[in] pointCode 测点编码。
     * @param[in] startTime 时间范围起点。
     * @param[in] endTime 时间范围终点。
     * @param[in] limit 结果数量上限。
     * @return 匹配条件的数据集合。
     */
    std::vector<std::pair<uint64_t, double>> getTimeSeriesData(const std::string& device_id,
                                                               const std::string& pointCode,
                                                               uint64_t startTime = 0,
                                                               uint64_t endTime = 0,
                                                               int limit = 1000);
    /**
     * @brief 清理指定时间范围的测点序列。
     * @param[in] device_id 设备 ID。
     * @param[in] pointCode 测点编码。
     * @param[in] beforeTime 清理截止时间。
     * @return true 表示操作成功；false 表示失败。
     */
    bool cleanTimeSeriesData(const std::string& device_id, const std::string& pointCode,
                             uint64_t beforeTime = 0);

    /**
     * @brief 将设备命令加入待执行队列。
     * @param[in] command 待执行的设备命令。
     * @return true 表示操作成功；false 表示失败。
     */
    bool pushCommand(const DeviceCommand& command);
    /**
     * @brief 从设备命令队列取出待执行命令。
     * @param[in] device_id 设备 ID。
     * @param[out] command 待执行的设备命令。
     * @param[in] timeoutMs 超时时间，单位为毫秒。
     * @return true 表示操作成功；false 表示失败。
     */
    bool popCommand(const std::string& device_id, DeviceCommand& command, int timeoutMs = 0);
    /**
     * @brief 确认设备命令的执行结果。
     * @param[in] commandId 命令标识。
     * @param[in] success 命令是否执行成功。
     * @param[in] message 命令结果说明。
     * @return true 表示操作成功；false 表示失败。
     */
    bool ackCommand(const std::string& commandId, bool success, const std::string& message = "");
    /**
     * @brief 查询设备命令的执行结果。
     * @param[in] commandId 命令标识。
     * @param[out] result 计算或执行结果。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getCommandResult(const std::string& commandId, CommandResult& result);

    /**
     * @brief 获取设备锁。
     * @param[in] device_id 设备 ID。
     * @param[in] ownerId 锁持有者标识。
     * @param[in] expireSeconds 过期时间，单位为秒。
     * @return true 表示操作成功；false 表示失败。
     */
    bool acquireLock(const std::string& device_id, const std::string& ownerId,
                     int expireSeconds = 10);
    /**
     * @brief 释放设备锁。
     * @param[in] device_id 设备 ID。
     * @param[in] ownerId 锁持有者标识。
     * @return true 表示操作成功；false 表示失败。
     */
    bool releaseLock(const std::string& device_id, const std::string& ownerId);
    /**
     * @brief 检查设备锁是否已被持有。
     * @param[in] device_id 设备 ID。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool isLocked(const std::string& device_id);
    /**
     * @brief 获取设备锁的持有者。
     * @param[in] device_id 设备 ID。
     * @return 对应的文本内容。
     */
    std::string getLockOwner(const std::string& device_id);

    /**
     * @brief 写入系统拓扑。
     * @param[in] topology 系统拓扑。
     * @return true 表示操作成功；false 表示失败。
     */
    bool setTopology(const SystemTopology& topology);
    /**
     * @brief 读取系统拓扑。
     * @param[in] topologyId 系统拓扑标识。
     * @param[out] topology 系统拓扑。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getTopology(const std::string& topologyId, SystemTopology& topology);
    /**
     * @brief 删除系统拓扑。
     * @param[in] topologyId 系统拓扑标识。
     * @return true 表示操作成功；false 表示失败。
     */
    bool deleteTopology(const std::string& topologyId);
    /**
     * @brief 读取全部系统拓扑。
     * @param[out] topologies 用于接收 topologies 的输出对象。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getAllTopologies(std::vector<SystemTopology>& topologies);

    /**
     * @brief 获取设备控制锁。
     * @param[in] device_id 设备 ID。
     * @param[in] ownerId 锁持有者标识。
     * @param[in] expireSeconds 过期时间，单位为秒。
     * @return true 表示操作成功；false 表示失败。
     */
    bool acquireControlLock(const std::string& device_id, const std::string& ownerId,
                            int expireSeconds = 10);
    /**
     * @brief 释放设备控制锁。
     * @param[in] device_id 设备 ID。
     * @param[in] ownerId 锁持有者标识。
     * @return true 表示操作成功；false 表示失败。
     */
    bool releaseControlLock(const std::string& device_id, const std::string& ownerId);
    /**
     * @brief 持有设备锁期间执行操作。
     * @param[in] device_id 设备 ID。
     * @param[in] ownerId 锁持有者标识。
     * @param[in] operation 待执行操作。
     * @param[in] timeoutMs 超时时间，单位为毫秒。
     * @return true 表示操作成功；false 表示失败。
     */
    bool executeWithLock(const std::string& device_id, const std::string& ownerId,
                         std::function<bool()> operation, int timeoutMs = 5000);

    /**
     * @brief 检查连接是否可用。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool isConnected() const { return rtdb_ && rtdb_->isConnected(); }

    /**
     * @brief 获取最近一次错误代码或说明。
     * @return 最近一次错误的说明文本。
     */
    std::string getLastError() const { return rtdb_ ? rtdb_->getLastError() : "Not initialized"; }

   private:
    /**
     * @brief 构造设备快照的 Redis 键。
     * @param[in] device_id 设备 ID。
     * @return 生成的存储键。
     */
    std::string buildDeviceSnapshotKey(const std::string& device_id) const;
    /**
     * @brief 构造设备测点的 Redis 键。
     * @param[in] device_id 设备 ID。
     * @param[in] pointCode 测点编码。
     * @return 生成的存储键。
     */
    std::string buildDevicePointKey(const std::string& device_id,
                                    const std::string& pointCode) const;
    /**
     * @brief 构造测点时间序列的 Redis 键。
     * @param[in] device_id 设备 ID。
     * @param[in] pointCode 测点编码。
     * @return 生成的存储键。
     */
    std::string buildTimeSeriesKey(const std::string& device_id,
                                   const std::string& pointCode) const;
    /**
     * @brief 构造命令队列的 Redis 键。
     * @param[in] device_id 设备 ID。
     * @return 生成的存储键。
     */
    std::string buildCommandQueueKey(const std::string& device_id) const;
    /**
     * @brief 构造命令结果的 Redis 键。
     * @param[in] commandId 命令标识。
     * @return 生成的存储键。
     */
    std::string buildCommandResultKey(const std::string& commandId) const;
    /**
     * @brief 构造设备锁的 Redis 键。
     * @param[in] device_id 设备 ID。
     * @return 生成的存储键。
     */
    std::string buildDeviceLockKey(const std::string& device_id) const;
    /**
     * @brief 构造设备控制锁的 Redis 键。
     * @param[in] device_id 设备 ID。
     * @return 生成的存储键。
     */
    std::string buildControlLockKey(const std::string& device_id) const;
    /**
     * @brief 构造系统拓扑的 Redis 键。
     * @param[in] topologyId 系统拓扑标识。
     * @return 生成的存储键。
     */
    std::string buildTopologyKey(const std::string& topologyId) const;
    /**
     * @brief 构造拓扑索引的 Redis 键。
     * @return 生成的存储键。
     */
    std::string buildTopologyIndexKey() const;

    /**
     * @brief 检查设备荷电状态是否允许放电。
     * @param[in] device_id 设备 ID。
     * @param[in] targetSoc 目标荷电状态。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool checkSocForDischarge(const std::string& device_id, double targetSoc);
    /**
     * @brief 校验荷电状态后执行设备控制。
     * @param[in] device_id 设备 ID。
     * @param[in] pointCode 测点编码。
     * @param[in] value 输入值。
     * @param[in] ownerId 锁持有者标识。
     * @return true 表示操作成功；false 表示失败。
     */
    bool executeControlWithSocCheck(const std::string& device_id, const std::string& pointCode,
                                    double value, const std::string& ownerId);

    std::unique_ptr<RtDb> rtdb_;
    RtDbConfig config_;
    bool initialized_{false};
};

/**
 * @brief 创建并初始化 EMS 实时数据库实例。
 * @param[in] config 配置数据。
 * @return 创建的对象；创建失败时为空。
 */
EMS_RTDB_API std::unique_ptr<RTDBEMS> createRtdbEms(const RtDbConfig& config);

}  // namespace rtdb
}  // namespace ems

#endif
