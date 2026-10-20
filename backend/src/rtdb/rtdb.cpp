#include "rtdb.h"

#include <sw/redis++/redis++.h>

#include <chrono>
#include <future>
#include <iterator>
#include <sstream>

namespace ems {
namespace rtdb {

RtDb::RtDb() : connected_(false), cmdCount_(0) {}

RtDb::~RtDb() { shutdown(); }

bool RtDb::initialize(const RtDbConfig& config) {
    config_ = config;
    if (config_.host.empty()) {
        config_.host = "localhost";
    }
    if (config_.password.empty()) {
        config_.password = "888888";
    }
    if (config_.port == 0) {
        config_.port = 6379;
    }
    if (config_.poolSize == 0) {
        config_.poolSize = 10;
    }
    if (config_.timeoutMs == 0) {
        config_.timeoutMs = 5000;
    }

    if (config_.useSentinel) {
        return connectSentinel();
    }
    return connect();
}

void RtDb::shutdown() { disconnect(); }

bool RtDb::connect() {
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        sw::redis::ConnectionOptions opts;
        opts.host = config_.host;
        opts.port = config_.port;

        if (!config_.password.empty()) {
            opts.password = config_.password;
        }

        if (config_.db != 0) {
            opts.db = config_.db;
        }

        opts.socket_timeout = std::chrono::milliseconds(config_.timeoutMs);
        opts.connect_timeout = std::chrono::milliseconds(config_.timeoutMs);

        sw::redis::ConnectionPoolOptions poolOpts;
        poolOpts.size = config_.poolSize;
        poolOpts.wait_timeout = std::chrono::milliseconds(config_.timeoutMs);

        redis_ = std::make_unique<sw::redis::Redis>(opts, poolOpts);

        redis_->ping();
        connected_ = true;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        connected_ = false;
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        connected_ = false;
        return false;
    }
}

bool RtDb::connectSentinel() {
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        sw::redis::SentinelOptions sentinelOpts;
        for (const auto& node : config_.sentinelNodes) {
            auto pos = node.find(':');
            if (pos != std::string::npos) {
                sentinelOpts.nodes.emplace_back(node.substr(0, pos),
                                                std::stoi(node.substr(pos + 1)));
            }
        }

        if (!config_.password.empty()) {
            sentinelOpts.password = config_.password;
        }

        sentinelOpts.connect_timeout = std::chrono::milliseconds(config_.timeoutMs);
        sentinelOpts.socket_timeout = std::chrono::milliseconds(config_.timeoutMs);

        sw::redis::ConnectionOptions connectionOpts;
        connectionOpts.host = config_.host;
        connectionOpts.port = config_.port;
        if (!config_.password.empty()) {
            connectionOpts.password = config_.password;
        }
        if (config_.db != 0) {
            connectionOpts.db = config_.db;
        }
        connectionOpts.socket_timeout = std::chrono::milliseconds(config_.timeoutMs);
        connectionOpts.connect_timeout = std::chrono::milliseconds(config_.timeoutMs);

        sw::redis::ConnectionPoolOptions poolOpts;
        poolOpts.size = config_.poolSize;
        poolOpts.wait_timeout = std::chrono::milliseconds(config_.timeoutMs);

        auto sentinel = std::make_shared<sw::redis::Sentinel>(sentinelOpts);
        redis_ =
            std::make_unique<sw::redis::Redis>(sentinel, config_.sentinelMasterName,
                                               sw::redis::Role::MASTER, connectionOpts, poolOpts);

        redis_->ping();
        connected_ = true;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        connected_ = false;
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        connected_ = false;
        return false;
    }
}

void RtDb::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    redis_.reset();
    connected_ = false;
}

bool RtDb::reconnect() {
    disconnect();
    if (config_.useSentinel) {
        return connectSentinel();
    }
    return connect();
}

bool RtDb::set(const std::string& key, const std::string& value) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = config_.prefix + key;
        redis_->set(fullKey, value);
        cmdCount_++;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::set(const std::string& key, double value) {
    std::ostringstream oss;
    oss << value;
    return set(key, oss.str());
}

bool RtDb::set(const std::string& key, int value) {
    std::ostringstream oss;
    oss << value;
    return set(key, oss.str());
}

bool RtDb::get(const std::string& key, std::string& value) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = config_.prefix + key;
        auto result = redis_->get(fullKey);
        if (result) {
            value = *result;
            cmdCount_++;
            return true;
        }
        return false;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::get(const std::string& key, double& value) {
    std::string str;
    if (get(key, str)) {
        try {
            value = std::stod(str);
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

bool RtDb::get(const std::string& key, int& value) {
    std::string str;
    if (get(key, str)) {
        try {
            value = std::stoi(str);
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

bool RtDb::setHash(const std::string& key, const std::map<std::string, std::string>& fields) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = config_.prefix + key;
        for (const auto& field : fields) {
            redis_->hset(fullKey, field.first, field.second);
        }
        cmdCount_ += fields.size();
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::getHash(const std::string& key, std::map<std::string, std::string>& fields) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = config_.prefix + key;
        redis_->hgetall(fullKey, std::inserter(fields, fields.end()));
        cmdCount_++;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::setHashField(const std::string& key, const std::string& field,
                        const std::string& value) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = config_.prefix + key;
        redis_->hset(fullKey, field, value);
        cmdCount_++;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::deleteKey(const std::string& key) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = config_.prefix + key;
        redis_->del(fullKey);
        cmdCount_++;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::exists(const std::string& key) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = config_.prefix + key;
        auto result = redis_->exists(fullKey);
        cmdCount_++;
        return result > 0;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

std::vector<std::string> RtDb::keys(const std::string& pattern) {
    std::vector<std::string> result;
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return result;
        }
    }

    try {
        std::string fullPattern = config_.prefix + pattern;
        redis_->keys(fullPattern, std::back_inserter(result));

        for (auto& key : result) {
            if (!config_.prefix.empty() && key.find(config_.prefix) == 0) {
                key = key.substr(config_.prefix.length());
            }
        }
        cmdCount_++;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
    } catch (const std::exception& e) {
        lastError_ = e.what();
    }
    return result;
}

bool RtDb::setWithExpire(const std::string& key, const std::string& value, int expireSeconds) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = config_.prefix + key;
        redis_->setex(fullKey, expireSeconds, value);
        cmdCount_++;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::expire(const std::string& key, int seconds) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = config_.prefix + key;
        redis_->expire(fullKey, seconds);
        cmdCount_++;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::append(const std::string& key, const std::string& value) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = config_.prefix + key;
        redis_->append(fullKey, value);
        cmdCount_++;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::incr(const std::string& key, int64_t& result) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = config_.prefix + key;
        result = redis_->incr(fullKey);
        cmdCount_++;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::incrByFloat(const std::string& key, double value, double& result) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = config_.prefix + key;
        result = redis_->incrbyfloat(fullKey, value);
        cmdCount_++;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::publish(const std::string& channel, const std::string& message) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullChannel = config_.prefix + channel;
        redis_->publish(fullChannel, message);
        cmdCount_++;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::subscribe(const std::string& channel,
                     std::function<void(const std::string&, const std::string&)> callback) {
    return false;
}

std::string RtDb::buildYcKey(const std::string& pointCode) const {
    return config_.prefix + "rtdb:yc:" + pointCode;
}

std::string RtDb::buildYxKey(const std::string& pointCode) const {
    return config_.prefix + "rtdb:yx:" + pointCode;
}

std::string RtDb::buildYkKey(const std::string& pointCode) const {
    return config_.prefix + "rtdb:yk:" + pointCode;
}

std::string RtDb::buildYtKey(const std::string& pointCode) const {
    return config_.prefix + "rtdb:yt:" + pointCode;
}

bool RtDb::setYc(const std::string& pointCode, const YcPoint& point) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = buildYcKey(pointCode);

        redis_->hset(fullKey, "v", std::to_string(point.value_));
        redis_->hset(fullKey, "t", std::to_string(point.timestamp_));
        redis_->hset(fullKey, "q", std::to_string(static_cast<int>(point.quality_)));
        redis_->hset(fullKey, "s", std::to_string(point.status_));

        cmdCount_ += 4;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::getYc(const std::string& pointCode, YcPoint& point) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = buildYcKey(pointCode);

        auto vResult = redis_->hget(fullKey, "v");
        auto tResult = redis_->hget(fullKey, "t");
        auto qResult = redis_->hget(fullKey, "q");
        auto sResult = redis_->hget(fullKey, "s");

        if (vResult) {
            point.value_ = std::stod(*vResult);

            if (tResult) {
                point.timestamp_ = std::stoull(*tResult);
            }

            if (qResult) {
                point.quality_ = static_cast<PointQuality>(std::stoi(*qResult));
            }

            if (sResult) {
                point.status_ = std::stoi(*sResult);
            }

            cmdCount_ += 4;
            return true;
        }
        return false;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

std::vector<YcPoint> RtDb::getYcPoints(const std::string& pattern) {
    std::vector<YcPoint> points;
    auto codes = getYcCodes(pattern);

    for (const auto& code : codes) {
        YcPoint point;
        if (getYc(code, point)) {
            points.push_back(point);
        }
    }
    return points;
}

bool RtDb::setYx(const std::string& pointCode, const YxPoint& point) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = buildYxKey(pointCode);

        redis_->hset(fullKey, "v", std::to_string(point.value_));
        redis_->hset(fullKey, "t", std::to_string(point.timestamp_));
        redis_->hset(fullKey, "q", std::to_string(static_cast<int>(point.quality_)));
        redis_->hset(fullKey, "s", std::to_string(point.status_));

        cmdCount_ += 4;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::getYx(const std::string& pointCode, YxPoint& point) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = buildYxKey(pointCode);

        auto vResult = redis_->hget(fullKey, "v");
        auto tResult = redis_->hget(fullKey, "t");
        auto qResult = redis_->hget(fullKey, "q");
        auto sResult = redis_->hget(fullKey, "s");

        if (vResult) {
            point.value_ = std::stoi(*vResult);

            if (tResult) {
                point.timestamp_ = std::stoull(*tResult);
            }

            if (qResult) {
                point.quality_ = static_cast<PointQuality>(std::stoi(*qResult));
            }

            if (sResult) {
                point.status_ = std::stoi(*sResult);
            }

            cmdCount_ += 4;
            return true;
        }
        return false;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

std::vector<YxPoint> RtDb::getYxPoints(const std::string& pattern) {
    std::vector<YxPoint> points;
    auto codes = getYxCodes(pattern);

    for (const auto& code : codes) {
        YxPoint point;
        if (getYx(code, point)) {
            points.push_back(point);
        }
    }
    return points;
}

bool RtDb::setYk(const std::string& pointCode, const YkPoint& point) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = buildYkKey(pointCode);

        redis_->hset(fullKey, "v", std::to_string(point.value_));
        redis_->hset(fullKey, "t", std::to_string(point.timestamp_));
        redis_->hset(fullKey, "c", std::to_string(point.commandType_));
        redis_->hset(fullKey, "s", std::to_string(static_cast<int>(point.status_)));
        redis_->hset(fullKey, "r", point.result_);

        cmdCount_ += 5;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::getYk(const std::string& pointCode, YkPoint& point) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = buildYkKey(pointCode);

        auto vResult = redis_->hget(fullKey, "v");
        auto tResult = redis_->hget(fullKey, "t");
        auto cResult = redis_->hget(fullKey, "c");
        auto sResult = redis_->hget(fullKey, "s");
        auto rResult = redis_->hget(fullKey, "r");

        if (vResult) {
            point.value_ = std::stoi(*vResult);

            if (tResult) {
                point.timestamp_ = std::stoull(*tResult);
            }

            if (cResult) {
                point.commandType_ = std::stoi(*cResult);
            }

            if (sResult) {
                point.status_ = static_cast<YKStatus>(std::stoi(*sResult));
            }

            if (rResult) {
                point.result_ = *rResult;
            }

            cmdCount_ += 5;
            return true;
        }
        return false;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

std::vector<YkPoint> RtDb::getYkPoints(const std::string& pattern) {
    std::vector<YkPoint> points;
    auto codes = getYkCodes(pattern);

    for (const auto& code : codes) {
        YkPoint point;
        if (getYk(code, point)) {
            points.push_back(point);
        }
    }
    return points;
}

bool RtDb::setYt(const std::string& pointCode, const YtPoint& point) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = buildYtKey(pointCode);

        redis_->hset(fullKey, "v", std::to_string(point.value_));
        redis_->hset(fullKey, "t", std::to_string(point.timestamp_));
        redis_->hset(fullKey, "q", std::to_string(static_cast<int>(point.quality_)));
        redis_->hset(fullKey, "s", std::to_string(point.status_));

        cmdCount_ += 4;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::getYt(const std::string& pointCode, YtPoint& point) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = buildYtKey(pointCode);

        auto vResult = redis_->hget(fullKey, "v");
        auto tResult = redis_->hget(fullKey, "t");
        auto qResult = redis_->hget(fullKey, "q");
        auto sResult = redis_->hget(fullKey, "s");

        if (vResult) {
            point.value_ = std::stod(*vResult);

            if (tResult) {
                point.timestamp_ = std::stoull(*tResult);
            }

            if (qResult) {
                point.quality_ = static_cast<PointQuality>(std::stoi(*qResult));
            }

            if (sResult) {
                point.status_ = std::stoi(*sResult);
            }

            cmdCount_ += 4;
            return true;
        }
        return false;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

std::vector<YtPoint> RtDb::getYtPoints(const std::string& pattern) {
    std::vector<YtPoint> points;
    auto codes = getYtCodes(pattern);

    for (const auto& code : codes) {
        YtPoint point;
        if (getYt(code, point)) {
            points.push_back(point);
        }
    }
    return points;
}

bool RtDb::deleteYc(const std::string& pointCode) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = buildYcKey(pointCode);
        redis_->del(fullKey);
        cmdCount_++;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::deleteYx(const std::string& pointCode) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = buildYxKey(pointCode);
        redis_->del(fullKey);
        cmdCount_++;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::deleteYk(const std::string& pointCode) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = buildYkKey(pointCode);
        redis_->del(fullKey);
        cmdCount_++;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::deleteYt(const std::string& pointCode) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = buildYtKey(pointCode);
        redis_->del(fullKey);
        cmdCount_++;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

std::vector<std::string> RtDb::getYcCodes(const std::string& pattern) {
    return keys("rtdb:yc:" + pattern);
}

std::vector<std::string> RtDb::getYxCodes(const std::string& pattern) {
    return keys("rtdb:yx:" + pattern);
}

std::vector<std::string> RtDb::getYkCodes(const std::string& pattern) {
    return keys("rtdb:yk:" + pattern);
}

std::vector<std::string> RtDb::getYtCodes(const std::string& pattern) {
    return keys("rtdb:yt:" + pattern);
}

bool RtDb::batchSetYc(const std::map<std::string, YcPoint>& points) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        auto pipe = redis_->pipeline();
        for (const auto& [code, point] : points) {
            std::string fullKey = buildYcKey(code);
            pipe.hset(fullKey, "v", std::to_string(point.value_));
            pipe.hset(fullKey, "t", std::to_string(point.timestamp_));
            pipe.hset(fullKey, "q", std::to_string(static_cast<int>(point.quality_)));
            pipe.hset(fullKey, "s", std::to_string(point.status_));
        }
        pipe.exec();
        cmdCount_ += points.size() * 4;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::batchSetYx(const std::map<std::string, YxPoint>& points) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        auto pipe = redis_->pipeline();
        for (const auto& [code, point] : points) {
            std::string fullKey = buildYxKey(code);
            pipe.hset(fullKey, "v", std::to_string(point.value_));
            pipe.hset(fullKey, "t", std::to_string(point.timestamp_));
            pipe.hset(fullKey, "q", std::to_string(static_cast<int>(point.quality_)));
            pipe.hset(fullKey, "s", std::to_string(point.status_));
        }
        pipe.exec();
        cmdCount_ += points.size() * 4;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::batchSetYk(const std::map<std::string, YkPoint>& points) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        auto pipe = redis_->pipeline();
        for (const auto& [code, point] : points) {
            std::string fullKey = buildYkKey(code);
            pipe.hset(fullKey, "v", std::to_string(point.value_));
            pipe.hset(fullKey, "t", std::to_string(point.timestamp_));
            pipe.hset(fullKey, "c", std::to_string(point.commandType_));
            pipe.hset(fullKey, "s", std::to_string(static_cast<int>(point.status_)));
            pipe.hset(fullKey, "r", point.result_);
        }
        pipe.exec();
        cmdCount_ += points.size() * 5;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::batchSetYt(const std::map<std::string, YtPoint>& points) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        auto pipe = redis_->pipeline();
        for (const auto& [code, point] : points) {
            std::string fullKey = buildYtKey(code);
            pipe.hset(fullKey, "v", std::to_string(point.value_));
            pipe.hset(fullKey, "t", std::to_string(point.timestamp_));
            pipe.hset(fullKey, "q", std::to_string(static_cast<int>(point.quality_)));
            pipe.hset(fullKey, "s", std::to_string(point.status_));
        }
        pipe.exec();
        cmdCount_ += points.size() * 4;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::batchGetYc(const std::vector<std::string>& codes, std::vector<YcPoint>& points) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        auto pipe = redis_->pipeline();

        for (const auto& code : codes) {
            std::string fullKey = buildYcKey(code);
            pipe.hget(fullKey, "v");
            pipe.hget(fullKey, "t");
            pipe.hget(fullKey, "q");
            pipe.hget(fullKey, "s");
        }

        auto replies = pipe.exec();

        for (size_t i = 0; i < codes.size(); i++) {
            YcPoint point;
            size_t base = i * 4;
            auto valueResult = replies.template get<sw::redis::OptionalString>(base);
            auto timeResult = replies.template get<sw::redis::OptionalString>(base + 1);
            auto qualResult = replies.template get<sw::redis::OptionalString>(base + 2);
            auto statResult = replies.template get<sw::redis::OptionalString>(base + 3);

            if (valueResult) {
                point.value_ = std::stod(*valueResult);
                if (timeResult) point.timestamp_ = std::stoull(*timeResult);
                if (qualResult) point.quality_ = static_cast<PointQuality>(std::stoi(*qualResult));
                if (statResult) point.status_ = std::stoi(*statResult);
                points.push_back(point);
            }
        }

        cmdCount_ += codes.size() * 4;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::batchGetYx(const std::vector<std::string>& codes, std::vector<YxPoint>& points) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        auto pipe = redis_->pipeline();

        for (const auto& code : codes) {
            std::string fullKey = buildYxKey(code);
            pipe.hget(fullKey, "v");
            pipe.hget(fullKey, "t");
            pipe.hget(fullKey, "q");
            pipe.hget(fullKey, "s");
        }

        auto replies = pipe.exec();

        for (size_t i = 0; i < codes.size(); i++) {
            YxPoint point;
            size_t base = i * 4;
            auto valueResult = replies.template get<sw::redis::OptionalString>(base);
            auto timeResult = replies.template get<sw::redis::OptionalString>(base + 1);
            auto qualResult = replies.template get<sw::redis::OptionalString>(base + 2);
            auto statResult = replies.template get<sw::redis::OptionalString>(base + 3);

            if (valueResult) {
                point.value_ = std::stoi(*valueResult);
                if (timeResult) point.timestamp_ = std::stoull(*timeResult);
                if (qualResult) point.quality_ = static_cast<PointQuality>(std::stoi(*qualResult));
                if (statResult) point.status_ = std::stoi(*statResult);
                points.push_back(point);
            }
        }

        cmdCount_ += codes.size() * 4;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::batchGetYk(const std::vector<std::string>& codes, std::vector<YkPoint>& points) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        auto pipe = redis_->pipeline();

        for (const auto& code : codes) {
            std::string fullKey = buildYkKey(code);
            pipe.hget(fullKey, "v");
            pipe.hget(fullKey, "t");
            pipe.hget(fullKey, "c");
            pipe.hget(fullKey, "s");
            pipe.hget(fullKey, "r");
        }

        auto replies = pipe.exec();

        for (size_t i = 0; i < codes.size(); i++) {
            YkPoint point;
            size_t base = i * 5;
            auto valueResult = replies.template get<sw::redis::OptionalString>(base);
            auto timeResult = replies.template get<sw::redis::OptionalString>(base + 1);
            auto cmdResult = replies.template get<sw::redis::OptionalString>(base + 2);
            auto statResult = replies.template get<sw::redis::OptionalString>(base + 3);
            auto resResult = replies.template get<sw::redis::OptionalString>(base + 4);

            if (valueResult) {
                point.value_ = std::stoi(*valueResult);
                if (timeResult) point.timestamp_ = std::stoull(*timeResult);
                if (cmdResult) point.commandType_ = std::stoi(*cmdResult);
                if (statResult) point.status_ = static_cast<YKStatus>(std::stoi(*statResult));
                if (resResult) point.result_ = *resResult;
                points.push_back(point);
            }
        }

        cmdCount_ += codes.size() * 5;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

bool RtDb::batchGetYt(const std::vector<std::string>& codes, std::vector<YtPoint>& points) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        auto pipe = redis_->pipeline();

        for (const auto& code : codes) {
            std::string fullKey = buildYtKey(code);
            pipe.hget(fullKey, "v");
            pipe.hget(fullKey, "t");
            pipe.hget(fullKey, "q");
            pipe.hget(fullKey, "s");
        }

        auto replies = pipe.exec();

        for (size_t i = 0; i < codes.size(); i++) {
            YtPoint point;
            size_t base = i * 4;
            auto valueResult = replies.template get<sw::redis::OptionalString>(base);
            auto timeResult = replies.template get<sw::redis::OptionalString>(base + 1);
            auto qualResult = replies.template get<sw::redis::OptionalString>(base + 2);
            auto statResult = replies.template get<sw::redis::OptionalString>(base + 3);

            if (valueResult) {
                point.value_ = std::stod(*valueResult);
                if (timeResult) point.timestamp_ = std::stoull(*timeResult);
                if (qualResult) point.quality_ = static_cast<PointQuality>(std::stoi(*qualResult));
                if (statResult) point.status_ = std::stoi(*statResult);
                points.push_back(point);
            }
        }

        cmdCount_ += codes.size() * 4;
        return true;
    } catch (const sw::redis::Error& e) {
        lastError_ = e.what();
        return false;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return false;
    }
}

std::future<bool> RtDb::asyncSet(const std::string& key, const std::string& value) {
    return std::async(std::launch::async, [this, key, value]() { return set(key, value); });
}

std::future<std::string> RtDb::asyncGet(const std::string& key) {
    return std::async(std::launch::async, [this, key]() {
        std::string value;
        if (get(key, value)) {
            return value;
        }
        return std::string();
    });
}

template <typename T>
std::string RtDb::pointHashFields(const T& point) {
    return "";
}

// ============================================================
// 协议类型转换（声明在 rtdb_channel.h，实现在此处）
// ============================================================

const char* protocolTypeToString(model::ProtocolType t) {
    switch (t) {
        case model::ProtocolType::ModbusRTU:
            return "ModbusRTU";
        case model::ProtocolType::ModbusTCP:
            return "ModbusTCP";
        case model::ProtocolType::IEC104:
            return "IEC104";
        case model::ProtocolType::DLT645:
            return "DLT645";
        case model::ProtocolType::DLT698:
            return "DLT698";
        case model::ProtocolType::IEC61850:
            return "IEC61850";
        case model::ProtocolType::OPCUA:
            return "OPCUA";
        case model::ProtocolType::MQTT:
            return "MQTT";
        case model::ProtocolType::CAN:
            return "CAN";
        case model::ProtocolType::Other:
            return "Other";
    }
    return "Unknown";
}

std::optional<model::ProtocolType> stringToProtocolType(const std::string& s) {
    if (s == "ModbusRTU") return model::ProtocolType::ModbusRTU;
    if (s == "ModbusTCP") return model::ProtocolType::ModbusTCP;
    if (s == "IEC104") return model::ProtocolType::IEC104;
    if (s == "DLT645") return model::ProtocolType::DLT645;
    if (s == "DLT698") return model::ProtocolType::DLT698;
    if (s == "IEC61850") return model::ProtocolType::IEC61850;
    if (s == "OPCUA") return model::ProtocolType::OPCUA;
    if (s == "MQTT") return model::ProtocolType::MQTT;
    if (s == "CAN") return model::ProtocolType::CAN;
    return std::nullopt;
}

// ============================================================
// createRtDb 工厂函数（声明在 rtdb.h）
// ============================================================

std::unique_ptr<RtDb> createRtDb(const RtDbConfig& config) {
    auto db = std::make_unique<RtDb>();
    if (db->initialize(config)) {
        return db;
    }
    return nullptr;
}

}  // namespace rtdb
}  // namespace ems
