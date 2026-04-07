#include "rtdb.h"
#include <sstream>
#include <chrono>
#include <iterator>
#include <sw/redis++/redis++.h>

namespace ems {
namespace net {

RTDB::RTDB()
    : connected_(false)
    , cmd_count_(0) {
}

RTDB::~RTDB() {
    shutdown();
}

bool RTDB::initialize(const RTDBConfig& config) {
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

void RTDB::shutdown() {
    disconnect();
}

bool RTDB::connect() {
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

void RTDB::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    redis_.reset();
    connected_ = false;
}

bool RTDB::reconnect() {
    disconnect();
    return connect();
}

bool RTDB::set(const std::string& key, const std::string& value) {
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

bool RTDB::set(const std::string& key, double value) {
    std::ostringstream oss;
    oss << value;
    return set(key, oss.str());
}

bool RTDB::set(const std::string& key, int value) {
    std::ostringstream oss;
    oss << value;
    return set(key, oss.str());
}

bool RTDB::get(const std::string& key, std::string& value) {
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

bool RTDB::get(const std::string& key, double& value) {
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

bool RTDB::get(const std::string& key, int& value) {
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

bool RTDB::setHash(const std::string& key, const std::map<std::string, std::string>& fields) {
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

bool RTDB::getHash(const std::string& key, std::map<std::string, std::string>& fields) {
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

bool RTDB::setHashField(const std::string& key, const std::string& field, const std::string& value) {
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

bool RTDB::deleteKey(const std::string& key) {
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

bool RTDB::exists(const std::string& key) {
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

std::vector<std::string> RTDB::keys(const std::string& pattern) {
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

bool RTDB::setWithExpire(const std::string& key, const std::string& value, int expire_seconds) {
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

bool RTDB::expire(const std::string& key, int seconds) {
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

bool RTDB::append(const std::string& key, const std::string& value) {
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

bool RTDB::incr(const std::string& key, int64_t& result) {
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

bool RTDB::incrByFloat(const std::string& key, double value, double& result) {
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

bool RTDB::publish(const std::string& channel, const std::string& message) {
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

bool RTDB::subscribe(const std::string& channel, std::function<void(const std::string&, const std::string&)> callback) {
    return false;
}

std::string RTDB::buildYcKey(const std::string& point_code) const {
    return config_.prefix + "rtdb:yc:" + point_code;
}

std::string RTDB::buildYxKey(const std::string& point_code) const {
    return config_.prefix + "rtdb:yx:" + point_code;
}

std::string RTDB::buildYkKey(const std::string& point_code) const {
    return config_.prefix + "rtdb:yk:" + point_code;
}

std::string RTDB::buildYtKey(const std::string& point_code) const {
    return config_.prefix + "rtdb:yt:" + point_code;
}

bool RTDB::setYC(const std::string& point_code, const YCPoint& point) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = buildYcKey(point_code);
        
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

bool RTDB::getYC(const std::string& point_code, YCPoint& point) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = buildYcKey(point_code);
        
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

std::vector<YCPoint> RTDB::getYCPoints(const std::string& pattern) {
    std::vector<YCPoint> points;
    auto codes = getYCCodes(pattern);
    
    for (const auto& code : codes) {
        YCPoint point;
        if (getYC(code, point)) {
            points.push_back(point);
        }
    }
    return points;
}

bool RTDB::setYX(const std::string& point_code, const YXPoint& point) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = buildYxKey(point_code);
        
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

bool RTDB::getYX(const std::string& point_code, YXPoint& point) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = buildYxKey(point_code);
        
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

std::vector<YXPoint> RTDB::getYXPoints(const std::string& pattern) {
    std::vector<YXPoint> points;
    auto codes = getYXCodes(pattern);
    
    for (const auto& code : codes) {
        YXPoint point;
        if (getYX(code, point)) {
            points.push_back(point);
        }
    }
    return points;
}

bool RTDB::setYK(const std::string& point_code, const YKPoint& point) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = buildYkKey(point_code);
        
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

bool RTDB::getYK(const std::string& point_code, YKPoint& point) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = buildYkKey(point_code);
        
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

std::vector<YKPoint> RTDB::getYKPoints(const std::string& pattern) {
    std::vector<YKPoint> points;
    auto codes = getYKCodes(pattern);
    
    for (const auto& code : codes) {
        YKPoint point;
        if (getYK(code, point)) {
            points.push_back(point);
        }
    }
    return points;
}

bool RTDB::setYT(const std::string& point_code, const YTPoint& point) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = buildYtKey(point_code);
        
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

bool RTDB::getYT(const std::string& point_code, YTPoint& point) {
    if (!connected_ || !redis_) {
        if (!reconnect()) {
            return false;
        }
    }

    try {
        std::string fullKey = buildYtKey(point_code);
        
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

std::vector<YTPoint> RTDB::getYTPoints(const std::string& pattern) {
    std::vector<YTPoint> points;
    auto codes = getYTCodes(pattern);
    
    for (const auto& code : codes) {
        YTPoint point;
        if (getYT(code, point)) {
            points.push_back(point);
        }
    }
    return points;
}

bool RTDB::deleteYC(const std::string& point_code) {
    return deleteKey(buildYcKey(point_code));
}

bool RTDB::deleteYX(const std::string& point_code) {
    return deleteKey(buildYxKey(point_code));
}

bool RTDB::deleteYK(const std::string& point_code) {
    return deleteKey(buildYkKey(point_code));
}

bool RTDB::deleteYT(const std::string& point_code) {
    return deleteKey(buildYtKey(point_code));
}

std::vector<std::string> RTDB::getYCCodes(const std::string& pattern) {
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

std::vector<std::string> RTDB::getYXCodes(const std::string& pattern) {
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

std::vector<std::string> RTDB::getYKCodes(const std::string& pattern) {
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

std::vector<std::string> RTDB::getYTCodes(const std::string& pattern) {
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

std::unique_ptr<RTDB> createRTDB(const RTDBConfig& config) {
    auto rtdb = std::make_unique<RTDB>();
    if (rtdb->initialize(config)) {
        return rtdb;
    }
    return nullptr;
}

}
}
