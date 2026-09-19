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
    RtDb();
    ~RtDb();

    bool initialize(const RtDbConfig& config);
    void shutdown();

    bool set(const std::string& key, const std::string& value);
    bool set(const std::string& key, double value);
    bool set(const std::string& key, int value);

    bool get(const std::string& key, std::string& value);
    bool get(const std::string& key, double& value);
    bool get(const std::string& key, int& value);

    bool setHash(const std::string& key, const std::map<std::string, std::string>& fields);
    bool getHash(const std::string& key, std::map<std::string, std::string>& fields);
    bool setHashField(const std::string& key, const std::string& field, const std::string& value);

    bool deleteKey(const std::string& key);
    bool exists(const std::string& key);
    std::vector<std::string> keys(const std::string& pattern);

    bool setWithExpire(const std::string& key, const std::string& value, int expireSeconds);
    bool expire(const std::string& key, int seconds);

    bool append(const std::string& key, const std::string& value);
    bool incr(const std::string& key, int64_t& result);
    bool incrByFloat(const std::string& key, double value, double& result);

    bool publish(const std::string& channel, const std::string& message);
    bool subscribe(const std::string& channel,
                   std::function<void(const std::string&, const std::string&)> callback);

    bool setYc(const std::string& pointCode, const YcPoint& point);
    bool getYc(const std::string& pointCode, YcPoint& point);
    std::vector<YcPoint> getYcPoints(const std::string& pattern);

    bool setYx(const std::string& pointCode, const YxPoint& point);
    bool getYx(const std::string& pointCode, YxPoint& point);
    std::vector<YxPoint> getYxPoints(const std::string& pattern);

    bool setYk(const std::string& pointCode, const YkPoint& point);
    bool getYk(const std::string& pointCode, YkPoint& point);
    std::vector<YkPoint> getYkPoints(const std::string& pattern);

    bool setYt(const std::string& pointCode, const YtPoint& point);
    bool getYt(const std::string& pointCode, YtPoint& point);
    std::vector<YtPoint> getYtPoints(const std::string& pattern);

    bool deleteYc(const std::string& pointCode);
    bool deleteYx(const std::string& pointCode);
    bool deleteYk(const std::string& pointCode);
    bool deleteYt(const std::string& pointCode);

    std::vector<std::string> getYcCodes(const std::string& pattern = "*");
    std::vector<std::string> getYxCodes(const std::string& pattern = "*");
    std::vector<std::string> getYkCodes(const std::string& pattern = "*");
    std::vector<std::string> getYtCodes(const std::string& pattern = "*");

    bool batchSetYc(const std::map<std::string, YcPoint>& points);
    bool batchSetYx(const std::map<std::string, YxPoint>& points);
    bool batchSetYk(const std::map<std::string, YkPoint>& points);
    bool batchSetYt(const std::map<std::string, YtPoint>& points);

    bool batchGetYc(const std::vector<std::string>& codes, std::vector<YcPoint>& points);
    bool batchGetYx(const std::vector<std::string>& codes, std::vector<YxPoint>& points);
    bool batchGetYk(const std::vector<std::string>& codes, std::vector<YkPoint>& points);
    bool batchGetYt(const std::vector<std::string>& codes, std::vector<YtPoint>& points);

    std::future<bool> asyncSet(const std::string& key, const std::string& value);
    std::future<std::string> asyncGet(const std::string& key);

    bool isConnected() const { return connected_.load(); }
    std::string getLastError() const { return lastError_; }
    uint64_t getCommandCount() const { return cmdCount_.load(); }

   private:
    bool connect();
    bool connectSentinel();
    void disconnect();
    bool reconnect();

    std::string buildYcKey(const std::string& pointCode) const;
    std::string buildYxKey(const std::string& pointCode) const;
    std::string buildYkKey(const std::string& pointCode) const;
    std::string buildYtKey(const std::string& pointCode) const;

    template <typename T>
    static std::string pointHashFields(const T& point);

    std::unique_ptr<sw::redis::Redis> redis_;
    RtDbConfig config_;
    std::atomic<bool> connected_;
    std::string lastError_;
    std::mutex mutex_;
    std::atomic<uint64_t> cmdCount_;
};

std::unique_ptr<RtDb> createRtDb(const RtDbConfig& config);

}  // namespace rtdb
}  // namespace ems

#endif