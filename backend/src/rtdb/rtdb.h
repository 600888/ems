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

struct RTDBConfig {
    std::string host;
    int port;
    std::string password;
    int db;
    int pool_size;
    int timeout_ms;
    std::string prefix;
};

class RTDB {
public:
    RTDB();
    ~RTDB();

    bool initialize(const RTDBConfig& config);
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

    bool setYC(const std::string& point_code, const YCPoint& point);
    bool getYC(const std::string& point_code, YCPoint& point);
    std::vector<YCPoint> getYCPoints(const std::string& pattern);

    bool setYX(const std::string& point_code, const YXPoint& point);
    bool getYX(const std::string& point_code, YXPoint& point);
    std::vector<YXPoint> getYXPoints(const std::string& pattern);

    bool setYK(const std::string& point_code, const YKPoint& point);
    bool getYK(const std::string& point_code, YKPoint& point);
    std::vector<YKPoint> getYKPoints(const std::string& pattern);

    bool setYT(const std::string& point_code, const YTPoint& point);
    bool getYT(const std::string& point_code, YTPoint& point);
    std::vector<YTPoint> getYTPoints(const std::string& pattern);

    bool deleteYC(const std::string& point_code);
    bool deleteYX(const std::string& point_code);
    bool deleteYK(const std::string& point_code);
    bool deleteYT(const std::string& point_code);

    std::vector<std::string> getYCCodes(const std::string& pattern = "*");
    std::vector<std::string> getYXCodes(const std::string& pattern = "*");
    std::vector<std::string> getYKCodes(const std::string& pattern = "*");
    std::vector<std::string> getYTCodes(const std::string& pattern = "*");

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
    RTDBConfig config_;
    std::atomic<bool> connected_;
    std::string last_error_;
    std::mutex mutex_;
    std::atomic<uint64_t> cmd_count_;
};

std::unique_ptr<RTDB> createRTDB(const RTDBConfig& config);

}
}

#endif
