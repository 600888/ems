#ifndef EMS_RTDB_H
#define EMS_RTDB_H

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "rtdb/rtdb_point.h"

namespace sw {
namespace redis {
class Redis;
}
}  // namespace sw

namespace ems {
namespace rtdb {

struct RtDbConfig {
    std::string host;
    int port;
    std::string password;
    int db;
    int poolSize;
    int timeoutMs;
    std::string prefix;

    bool useSentinel{false};
    std::string sentinelMasterName;
    std::vector<std::string> sentinelNodes;
};

class RtDb {
   public:
    /** @brief 构造 RtDb 实例。 */
    RtDb();
    /** @brief 释放 RtDb 持有的资源。 */
    ~RtDb();

    /**
     * @brief 初始化连接和运行配置。
     * @param[in] config 配置数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool initialize(const RtDbConfig& config);
    /** @brief 停止服务并释放资源。 */
    void shutdown();

    /**
     * @brief 写入字符串键值。
     * @param[in] key Redis 键。
     * @param[in] value 待写入的值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool set(const std::string& key, const std::string& value);
    /**
     * @brief 写入浮点键值。
     * @param[in] key Redis 键。
     * @param[in] value 待写入的值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool set(const std::string& key, double value);
    /**
     * @brief 写入整数键值。
     * @param[in] key Redis 键。
     * @param[in] value 待写入的值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool set(const std::string& key, int value);

    /**
     * @brief 读取指定键的值。
     * @param[in] key Redis 键。
     * @param[out] value 接收读取结果的值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool get(const std::string& key, std::string& value);
    /**
     * @brief 读取指定键的值。
     * @param[in] key Redis 键。
     * @param[out] value 接收读取结果的值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool get(const std::string& key, double& value);
    /**
     * @brief 读取指定键的值。
     * @param[in] key Redis 键。
     * @param[out] value 接收读取结果的值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool get(const std::string& key, int& value);

    /**
     * @brief 写入 Redis 哈希的全部字段。
     * @param[in] key Redis 键。
     * @param[in] fields 字段映射。
     * @return true 表示操作成功；false 表示失败。
     */
    bool setHash(const std::string& key, const std::map<std::string, std::string>& fields);
    /**
     * @brief 读取 Redis 哈希的全部字段。
     * @param[in] key Redis 键。
     * @param[out] fields 字段映射。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getHash(const std::string& key, std::map<std::string, std::string>& fields);
    /**
     * @brief 写入 Redis 哈希的指定字段。
     * @param[in] key Redis 键。
     * @param[in] field 哈希字段名。
     * @param[in] value 待写入的值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool setHashField(const std::string& key, const std::string& field, const std::string& value);

    /**
     * @brief 删除指定 Redis 键。
     * @param[in] key Redis 键。
     * @return true 表示操作成功；false 表示失败。
     */
    bool deleteKey(const std::string& key);
    /**
     * @brief 检查指定 Redis 键是否存在。
     * @param[in] key Redis 键。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool exists(const std::string& key);
    /**
     * @brief 查询匹配模式的 Redis 键。
     * @param[in] pattern 键或测点编码的匹配模式。
     * @return 匹配条件的数据集合。
     */
    std::vector<std::string> keys(const std::string& pattern);

    /**
     * @brief 写入带过期时间的 Redis 键值。
     * @param[in] key Redis 键。
     * @param[in] value 待写入的值。
     * @param[in] expireSeconds 过期时间，单位为秒。
     * @return true 表示操作成功；false 表示失败。
     */
    bool setWithExpire(const std::string& key, const std::string& value, int expireSeconds);
    /**
     * @brief 设置 Redis 键的过期时间。
     * @param[in] key Redis 键。
     * @param[in] seconds 时间长度，单位为秒。
     * @return true 表示操作成功；false 表示失败。
     */
    bool expire(const std::string& key, int seconds);

    /**
     * @brief 向 Redis 字符串末尾追加内容。
     * @param[in] key Redis 键。
     * @param[in] value 输入值。
     * @return true 表示操作成功；false 表示失败。
     */
    bool append(const std::string& key, const std::string& value);
    /**
     * @brief 将 Redis 整数值递增并返回结果。
     * @param[in] key Redis 键。
     * @param[out] result 计算或执行结果。
     * @return true 表示操作成功；false 表示失败。
     */
    bool incr(const std::string& key, int64_t& result);
    /**
     * @brief 将 Redis 浮点值递增并返回结果。
     * @param[in] key Redis 键。
     * @param[in] value 输入值。
     * @param[out] result 计算或执行结果。
     * @return true 表示操作成功；false 表示失败。
     */
    bool incrByFloat(const std::string& key, double value, double& result);

    /**
     * @brief 向指定 Redis 频道发布消息。
     * @param[in] channel 通道对象。
     * @param[in] message 命令结果说明。
     * @return true 表示操作成功；false 表示失败。
     */
    bool publish(const std::string& channel, const std::string& message);
    /**
     * @brief 订阅指定 Redis 频道的消息。
     * @param[in] channel 通道对象。
     * @param[in] callback 操作完成或变化时调用的回调。
     * @return true 表示操作成功；false 表示失败。
     */
    bool subscribe(const std::string& channel,
                   std::function<void(const std::string&, const std::string&)> callback);

    /**
     * @brief 写入遥测测点数据。
     * @param[in] pointCode 测点编码。
     * @param[in] point 测点数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool setYc(const std::string& pointCode, const YcPoint& point);
    /**
     * @brief 读取遥测测点数据。
     * @param[in] pointCode 测点编码。
     * @param[out] point 测点数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getYc(const std::string& pointCode, YcPoint& point);
    /**
     * @brief 读取遥测测点。
     * @param[in] pattern 键或测点编码的匹配模式。
     * @return 匹配条件的数据集合。
     */
    std::vector<YcPoint> getYcPoints(const std::string& pattern);

    /**
     * @brief 写入遥信测点数据。
     * @param[in] pointCode 测点编码。
     * @param[in] point 测点数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool setYx(const std::string& pointCode, const YxPoint& point);
    /**
     * @brief 读取遥信测点数据。
     * @param[in] pointCode 测点编码。
     * @param[out] point 测点数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getYx(const std::string& pointCode, YxPoint& point);
    /**
     * @brief 读取遥信测点。
     * @param[in] pattern 键或测点编码的匹配模式。
     * @return 匹配条件的数据集合。
     */
    std::vector<YxPoint> getYxPoints(const std::string& pattern);

    /**
     * @brief 写入遥控测点数据。
     * @param[in] pointCode 测点编码。
     * @param[in] point 测点数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool setYk(const std::string& pointCode, const YkPoint& point);
    /**
     * @brief 读取遥控测点数据。
     * @param[in] pointCode 测点编码。
     * @param[out] point 测点数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getYk(const std::string& pointCode, YkPoint& point);
    /**
     * @brief 读取遥控测点。
     * @param[in] pattern 键或测点编码的匹配模式。
     * @return 匹配条件的数据集合。
     */
    std::vector<YkPoint> getYkPoints(const std::string& pattern);

    /**
     * @brief 写入遥调测点数据。
     * @param[in] pointCode 测点编码。
     * @param[in] point 测点数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool setYt(const std::string& pointCode, const YtPoint& point);
    /**
     * @brief 读取遥调测点数据。
     * @param[in] pointCode 测点编码。
     * @param[out] point 测点数据。
     * @return true 表示操作成功；false 表示失败。
     */
    bool getYt(const std::string& pointCode, YtPoint& point);
    /**
     * @brief 读取遥调测点。
     * @param[in] pattern 键或测点编码的匹配模式。
     * @return 匹配条件的数据集合。
     */
    std::vector<YtPoint> getYtPoints(const std::string& pattern);

    /**
     * @brief 删除遥测测点数据。
     * @param[in] pointCode 测点编码。
     * @return true 表示操作成功；false 表示失败。
     */
    bool deleteYc(const std::string& pointCode);
    /**
     * @brief 删除遥信测点数据。
     * @param[in] pointCode 测点编码。
     * @return true 表示操作成功；false 表示失败。
     */
    bool deleteYx(const std::string& pointCode);
    /**
     * @brief 删除遥控测点数据。
     * @param[in] pointCode 测点编码。
     * @return true 表示操作成功；false 表示失败。
     */
    bool deleteYk(const std::string& pointCode);
    /**
     * @brief 删除遥调测点数据。
     * @param[in] pointCode 测点编码。
     * @return true 表示操作成功；false 表示失败。
     */
    bool deleteYt(const std::string& pointCode);

    /**
     * @brief 读取遥测测点编码。
     * @param[in] pattern 键或测点编码的匹配模式。
     * @return 匹配条件的数据集合。
     */
    std::vector<std::string> getYcCodes(const std::string& pattern = "*");
    /**
     * @brief 读取遥信测点编码。
     * @param[in] pattern 键或测点编码的匹配模式。
     * @return 匹配条件的数据集合。
     */
    std::vector<std::string> getYxCodes(const std::string& pattern = "*");
    /**
     * @brief 读取遥控测点编码。
     * @param[in] pattern 键或测点编码的匹配模式。
     * @return 匹配条件的数据集合。
     */
    std::vector<std::string> getYkCodes(const std::string& pattern = "*");
    /**
     * @brief 读取遥调测点编码。
     * @param[in] pattern 键或测点编码的匹配模式。
     * @return 匹配条件的数据集合。
     */
    std::vector<std::string> getYtCodes(const std::string& pattern = "*");

    /**
     * @brief 批量写入遥测测点数据。
     * @param[in] points 测点数据列表。
     * @return true 表示操作成功；false 表示失败。
     */
    bool batchSetYc(const std::map<std::string, YcPoint>& points);
    /**
     * @brief 批量写入遥信测点数据。
     * @param[in] points 测点数据列表。
     * @return true 表示操作成功；false 表示失败。
     */
    bool batchSetYx(const std::map<std::string, YxPoint>& points);
    /**
     * @brief 批量写入遥控测点数据。
     * @param[in] points 测点数据列表。
     * @return true 表示操作成功；false 表示失败。
     */
    bool batchSetYk(const std::map<std::string, YkPoint>& points);
    /**
     * @brief 批量写入遥调测点数据。
     * @param[in] points 测点数据列表。
     * @return true 表示操作成功；false 表示失败。
     */
    bool batchSetYt(const std::map<std::string, YtPoint>& points);

    /**
     * @brief 批量读取遥测测点数据。
     * @param[in] codes 待处理的编码列表。
     * @param[out] points 测点数据列表。
     * @return true 表示操作成功；false 表示失败。
     */
    bool batchGetYc(const std::vector<std::string>& codes, std::vector<YcPoint>& points);
    /**
     * @brief 批量读取遥信测点数据。
     * @param[in] codes 待处理的编码列表。
     * @param[out] points 测点数据列表。
     * @return true 表示操作成功；false 表示失败。
     */
    bool batchGetYx(const std::vector<std::string>& codes, std::vector<YxPoint>& points);
    /**
     * @brief 批量读取遥控测点数据。
     * @param[in] codes 待处理的编码列表。
     * @param[out] points 测点数据列表。
     * @return true 表示操作成功；false 表示失败。
     */
    bool batchGetYk(const std::vector<std::string>& codes, std::vector<YkPoint>& points);
    /**
     * @brief 批量读取遥调测点数据。
     * @param[in] codes 待处理的编码列表。
     * @param[out] points 测点数据列表。
     * @return true 表示操作成功；false 表示失败。
     */
    bool batchGetYt(const std::vector<std::string>& codes, std::vector<YtPoint>& points);

    /**
     * @brief 异步写入 Redis 键值。
     * @param[in] key Redis 键。
     * @param[in] value 输入值。
     * @return 用于获取异步操作结果的 future。
     */
    std::future<bool> asyncSet(const std::string& key, const std::string& value);
    /**
     * @brief 异步读取 Redis 键值。
     * @param[in] key Redis 键。
     * @return 用于获取异步操作结果的 future。
     */
    std::future<std::string> asyncGet(const std::string& key);

    /**
     * @brief 检查连接是否可用。
     * @return true 表示条件成立；false 表示不成立。
     */
    bool isConnected() const { return connected_.load(); }

    /**
     * @brief 获取最近一次错误代码或说明。
     * @return 最近一次错误的说明文本。
     */
    std::string getLastError() const { return lastError_; }

    /**
     * @brief 获取已执行的 Redis 命令数。
     * @return 处理或统计得到的数据项数量。
     */
    uint64_t getCommandCount() const { return cmdCount_.load(); }

   private:
    /**
     * @brief 建立连接。
     * @return true 表示操作成功；false 表示失败。
     */
    bool connect();
    /**
     * @brief 通过 Redis Sentinel 建立连接。
     * @return true 表示操作成功；false 表示失败。
     */
    bool connectSentinel();
    /** @brief 断开连接。 */
    void disconnect();
    /**
     * @brief 重新建立连接。
     * @return true 表示操作成功；false 表示失败。
     */
    bool reconnect();

    /**
     * @brief 构造对应数据的 Redis 键。
     * @param[in] pointCode 测点编码。
     * @return 用于存储该记录的物理键。
     */
    std::string buildYcKey(const std::string& pointCode) const;
    /**
     * @brief 构造对应数据的 Redis 键。
     * @param[in] pointCode 测点编码。
     * @return 用于存储该记录的物理键。
     */
    std::string buildYxKey(const std::string& pointCode) const;
    /**
     * @brief 构造对应数据的 Redis 键。
     * @param[in] pointCode 测点编码。
     * @return 用于存储该记录的物理键。
     */
    std::string buildYkKey(const std::string& pointCode) const;
    /**
     * @brief 构造对应数据的 Redis 键。
     * @param[in] pointCode 测点编码。
     * @return 用于存储该记录的物理键。
     */
    std::string buildYtKey(const std::string& pointCode) const;

    /**
     * @brief 将测点字段序列化为哈希数据。
     * @param[in] point 测点数据。
     * @return 对应的文本内容。
     */
    template <typename T>
    static std::string pointHashFields(const T& point);

    std::unique_ptr<sw::redis::Redis> redis_;
    RtDbConfig config_;
    std::atomic<bool> connected_;
    std::string lastError_;
    std::mutex mutex_;
    std::atomic<uint64_t> cmdCount_;
};

/**
 * @brief 创建并初始化 RTDB 实例。
 * @param[in] config 配置数据。
 * @return 创建的对象；创建失败时为空。
 */
std::unique_ptr<RtDb> createRtDb(const RtDbConfig& config);

}  // namespace rtdb
}  // namespace ems

#endif
