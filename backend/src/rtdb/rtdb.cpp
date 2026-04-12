#include "rtdb.h"
#include <sstream>
#include <chrono>
#include <iterator>
#include <sw/redis++/redis++.h>

namespace ems {
namespace net {

RtDb::RtDb()
    : connected_(false)
    , cmd_count_(0) {
}

RtDb::~RtDb() {
    shutdown();
}

bool RtDb::initialize(const RtDbConfig& config) {
    config_ = config;
    if (config_.host.empty()) {
        config_.host = "localhost";
    }
    if(config_.password.empty()) {
        config_.password = "888888";
    }
    if (config_.port == 0) {
        config_.port = 6379;
    }
    if (config_.pool_size == 0) {
        config_.pool_size = 10;
    }
    if (config_.timeout_ms == 0) {
        config_.timeout_ms = 5000;
    }

    return connect();
}

void RtDb::shutdown() {
    disconnect();
}

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

        opts.socket_timeout = std::chrono::milliseconds(config_.timeout_ms);
        opts.connect_timeout = std::chrono::milliseconds(config_.timeout_ms);

        sw::redis::ConnectionPoolOptions pool_opts;
        pool_opts.size = config_.pool_size;
        pool_opts.wait_timeout = std::chrono::milliseconds(config_.timeout_ms);

        redis_ = std::make_unique<sw::redis::Redis>(opts, pool_opts);
        
        redis_->ping();
        connected_ = true;
        return true;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        connected_ = false;
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
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
        cmd_count_++;
        return true;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
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
            cmd_count_++;
            return true;
        }
        return false;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
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
        cmd_count_ += fields.size();
        return true;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
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
        cmd_count_++;
        return true;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return false;
    }
}

bool RtDb::setHashField(const std::string& key, const std::string& field, const std::string& value) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = config_.prefix + key;
        redis_->hset(fullKey, field, value);
        cmd_count_++;
        return true;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
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
        cmd_count_++;
        return true;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
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
        cmd_count_++;
        return result > 0;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
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
        cmd_count_++;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
    } catch (const std::exception& e) {
        last_error_ = e.what();
    }
    return result;
}

bool RtDb::setWithExpire(const std::string& key, const std::string& value, int expire_seconds) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = config_.prefix + key;
        redis_->setex(fullKey, expire_seconds, value);
        cmd_count_++;
        return true;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
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
        cmd_count_++;
        return true;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
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
        cmd_count_++;
        return true;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
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
        cmd_count_++;
        return true;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
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
        cmd_count_++;
        return true;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
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
        cmd_count_++;
        return true;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return false;
    }
}

bool RtDb::subscribe(const std::string& channel, std::function<void(const std::string&, const std::string&)> callback) {
    return false;
}

std::string RtDb::buildYcKey(const std::string& point_code) const {
    return config_.prefix + "rtdb:yc:" + point_code;
}

std::string RtDb::buildYxKey(const std::string& point_code) const {
    return config_.prefix + "rtdb:yx:" + point_code;
}

std::string RtDb::buildYkKey(const std::string& point_code) const {
    return config_.prefix + "rtdb:yk:" + point_code;
}

std::string RtDb::buildYtKey(const std::string& point_code) const {
    return config_.prefix + "rtdb:yt:" + point_code;
}

bool RtDb::setYc(const std::string& pointCode, const YcPoint& point) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = buildYcKey(pointCode);
        
        redis_->hset(fullKey, "v", std::to_string(point.value));
        redis_->hset(fullKey, "t", std::to_string(point.timestamp));
        redis_->hset(fullKey, "q", std::to_string(point.quality));
        redis_->hset(fullKey, "s", std::to_string(point.status));
        
        cmd_count_ += 4;
        return true;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
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
        
        auto v_result = redis_->hget(fullKey, "v");
        auto t_result = redis_->hget(fullKey, "t");
        auto q_result = redis_->hget(fullKey, "q");
        auto s_result = redis_->hget(fullKey, "s");

        if (v_result) {
            point.value = std::stod(*v_result);
            
            if (t_result) {
                point.timestamp = std::stoull(*t_result);
            }
            
            if (q_result) {
                point.quality = std::stoi(*q_result);
            }
            
            if (s_result) {
                point.status = std::stoi(*s_result);
            }
            
            cmd_count_ += 4;
            return true;
        }
        return false;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return false;
    }
}

std::vector<YcPoint> RtDb::getYCPoints(const std::string& pattern) {
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
        
        redis_->hset(fullKey, "v", std::to_string(point.value));
        redis_->hset(fullKey, "t", std::to_string(point.timestamp));
        redis_->hset(fullKey, "q", std::to_string(point.quality));
        redis_->hset(fullKey, "s", std::to_string(point.status));
        
        cmd_count_ += 4;
        return true;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
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
        
        auto v_result = redis_->hget(fullKey, "v");
        auto t_result = redis_->hget(fullKey, "t");
        auto q_result = redis_->hget(fullKey, "q");
        auto s_result = redis_->hget(fullKey, "s");

        if (v_result) {
            point.value = std::stoi(*v_result);
            
            if (t_result) {
                point.timestamp = std::stoull(*t_result);
            }
            
            if (q_result) {
                point.quality = std::stoi(*q_result);
            }
            
            if (s_result) {
                point.status = std::stoi(*s_result);
            }
            
            cmd_count_ += 4;
            return true;
        }
        return false;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return false;
    }
}

std::vector<YxPoint> RtDb::getYXPoints(const std::string& pattern) {
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
        
        redis_->hset(fullKey, "v", std::to_string(point.value));
        redis_->hset(fullKey, "t", std::to_string(point.timestamp));
        redis_->hset(fullKey, "c", std::to_string(point.command_type));
        redis_->hset(fullKey, "s", std::to_string(static_cast<int>(point.status)));
        redis_->hset(fullKey, "r", std::to_string(point.result));
        
        cmd_count_ += 5;
        return true;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
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
        
        auto v_result = redis_->hget(fullKey, "v");
        auto t_result = redis_->hget(fullKey, "t");
        auto c_result = redis_->hget(fullKey, "c");
        auto s_result = redis_->hget(fullKey, "s");
        auto r_result = redis_->hget(fullKey, "r");

        if (v_result) {
            point.value = std::stoi(*v_result);
            
            if (t_result) {
                point.timestamp = std::stoull(*t_result);
            }
            
            if (c_result) {
                point.command_type = std::stoi(*c_result);
            }
            
            if (s_result) {
                point.status = static_cast<YKStatus>(std::stoi(*s_result));
            }
            
            if (r_result) {
                point.result = std::stoi(*r_result);
            }
            
            cmd_count_ += 5;
            return true;
        }
        return false;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return false;
    }
}

std::vector<YkPoint> RtDb::getYKPoints(const std::string& pattern) {
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
        
        redis_->hset(fullKey, "v", std::to_string(point.value));
        redis_->hset(fullKey, "t", std::to_string(point.timestamp));
        redis_->hset(fullKey, "q", std::to_string(point.quality));
        redis_->hset(fullKey, "s", std::to_string(point.status));
        
        cmd_count_ += 4;
        return true;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
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
        
        auto v_result = redis_->hget(fullKey, "v");
        auto t_result = redis_->hget(fullKey, "t");
        auto q_result = redis_->hget(fullKey, "q");
        auto s_result = redis_->hget(fullKey, "s");

        if (v_result) {
            point.value = std::stod(*v_result);
            
            if (t_result) {
                point.timestamp = std::stoull(*t_result);
            }
            
            if (q_result) {
                point.quality = std::stoi(*q_result);
            }
            
            if (s_result) {
                point.status = std::stoi(*s_result);
            }
            
            cmd_count_ += 4;
            return true;
        }
        return false;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
        return false;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return false;
    }
}

std::vector<YtPoint> RtDb::getYTPoints(const std::string& pattern) {
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
    return deleteKey(buildYcKey(pointCode));
}

bool RtDb::deleteYx(const std::string& pointCode) {
    return deleteKey(buildYxKey(pointCode));
}

bool RtDb::deleteYk(const std::string& pointCode) {
    return deleteKey(buildYkKey(pointCode));
}

bool RtDb::deleteYt(const std::string& pointCode) {
    return deleteKey(buildYtKey(pointCode));
}

std::vector<std::string> RtDb::getYcCodes(const std::string& pattern) {
    std::vector<std::string> codes;
    auto fullPattern = config_.prefix + "rtdb:yc:" + pattern;
    
    try {
        std::vector<std::string> keys;
        redis_->keys(fullPattern, std::back_inserter(keys));
        
        for (const auto& key : keys) {
            std::string code = key;
            auto pos = code.rfind(':');
            if (pos != std::string::npos) {
                code = code.substr(pos + 1);
                codes.push_back(code);
            }
        }
        cmd_count_++;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
    } catch (const std::exception& e) {
        last_error_ = e.what();
    }
    return codes;
}

std::vector<std::string> RtDb::getYxCodes(const std::string& pattern) {
    std::vector<std::string> codes;
    auto fullPattern = config_.prefix + "rtdb:yx:" + pattern;
    
    try {
        std::vector<std::string> keys;
        redis_->keys(fullPattern, std::back_inserter(keys));
        
        for (const auto& key : keys) {
            std::string code = key;
            auto pos = code.rfind(':');
            if (pos != std::string::npos) {
                code = code.substr(pos + 1);
                codes.push_back(code);
            }
        }
        cmd_count_++;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
    } catch (const std::exception& e) {
        last_error_ = e.what();
    }
    return codes;
}

std::vector<std::string> RtDb::getYkCodes(const std::string& pattern) {
    std::vector<std::string> codes;
    auto fullPattern = config_.prefix + "rtdb:yk:" + pattern;
    
    try {
        std::vector<std::string> keys;
        redis_->keys(fullPattern, std::back_inserter(keys));
        
        for (const auto& key : keys) {
            std::string code = key;
            auto pos = code.rfind(':');
            if (pos != std::string::npos) {
                code = code.substr(pos + 1);
                codes.push_back(code);
            }
        }
        cmd_count_++;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
    } catch (const std::exception& e) {
        last_error_ = e.what();
    }
    return codes;
}

std::vector<std::string> RtDb::getYtCodes(const std::string& pattern) {
    std::vector<std::string> codes;
    auto fullPattern = config_.prefix + "rtdb:yt:" + pattern;
    
    try {
        std::vector<std::string> keys;
        redis_->keys(fullPattern, std::back_inserter(keys));
        
        for (const auto& key : keys) {
            std::string code = key;
            auto pos = code.rfind(':');
            if (pos != std::string::npos) {
                code = code.substr(pos + 1);
                codes.push_back(code);
            }
        }
        cmd_count_++;
    } catch (const sw::redis::Error& e) {
        last_error_ = e.what();
    } catch (const std::exception& e) {
        last_error_ = e.what();
    }
    return codes;
}

std::unique_ptr<RtDb> createRtDb(const RtDbConfig& config) {
    auto rtdb = std::make_unique<RtDb>();
    if (rtdb->initialize(config)) {
        return rtdb;
    }
    return nullptr;
}

}
}
