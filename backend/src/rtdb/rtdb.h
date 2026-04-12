#ifndef EMS_RTDB_H
#define EMS_RTDB_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <chrono>
#include "rtdb/rtdb_point.h"

namespace sw {
namespace redis {
class Redis;
}
}

namespace ems {
namespace net {

struct RtDbConfig {
    std::string host;
    int port;
    std::string password;
    int db;
    int pool_size;
    int timeout_ms;
    std::string prefix;
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

    bool setWithExpire(const std::string& key, const std::string& value, int expire_seconds);
    bool expire(const std::string& key, int seconds);

    bool append(const std::string& key, const std::string& value);
    bool incr(const std::string& key, int64_t& result);
    bool incrByFloat(const std::string& key, double value, double& result);

    bool publish(const std::string& channel, const std::string& message);
    bool subscribe(const std::string& channel, std::function<void(const std::string&, const std::string&)> callback);

    bool setYc(const std::string& pointCode, const YcPoint& point);
    bool getYc(const std::string& pointCode, YcPoint& point);
    std::vector<YcPoint> getYCPoints(const std::string& pattern);

    bool setYx(const std::string& pointCode, const YxPoint& point);
    bool getYx(const std::string& pointCode, YxPoint& point);
    std::vector<YxPoint> getYXPoints(const std::string& pattern);

    bool setYk(const std::string& pointCode, const YkPoint& point);
    bool getYk(const std::string& pointCode, YkPoint& point);
    std::vector<YkPoint> getYKPoints(const std::string& pattern);

    bool setYt(const std::string& pointCode, const YtPoint& point);
    bool getYt(const std::string& pointCode, YtPoint& point);
    std::vector<YtPoint> getYTPoints(const std::string& pattern);

    bool deleteYc(const std::string& pointCode);
    bool deleteYx(const std::string& pointCode);
    bool deleteYk(const std::string& pointCode);
    bool deleteYt(const std::string& pointCode);

    std::vector<std::string> getYcCodes(const std::string& pattern = "*");
    std::vector<std::string> getYxCodes(const std::string& pattern = "*");
    std::vector<std::string> getYkCodes(const std::string& pattern = "*");
    std::vector<std::string> getYtCodes(const std::string& pattern = "*");

    bool isConnected() const { return connected_.load(); }
    std::string getLastError() const { return last_error_; }

private:
    bool connect();
    void disconnect();
    bool reconnect();

    std::string buildYcKey(const std::string& point_code) const;
    std::string buildYxKey(const std::string& point_code) const;
    std::string buildYkKey(const std::string& point_code) const;
    std::string buildYtKey(const std::string& point_code) const;

    std::unique_ptr<sw::redis::Redis> redis_;
    RtDbConfig config_;
    std::atomic<bool> connected_;
    std::string last_error_;
    std::mutex mutex_;
    std::atomic<uint64_t> cmd_count_;
};

std::unique_ptr<RtDb> createRtDb(const RtDbConfig& config);

}
}

#endif
