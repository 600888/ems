#include "rtdb_point.h"
#include <sstream>
#include <algorithm>

namespace ems {
namespace rtdb {

static std::string escapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

static std::string decodeCodeToString(DecodeCode code) {
    switch (code) {
        case DecodeCode::UINT16: return "UINT16";
        case DecodeCode::INT16: return "INT16";
        case DecodeCode::UINT32: return "UINT32";
        case DecodeCode::INT32: return "INT32";
        case DecodeCode::FLOAT: return "FLOAT";
        case DecodeCode::DOUBLE: return "DOUBLE";
        case DecodeCode::UINT8: return "UINT8";
        case DecodeCode::INT8: return "INT8";
        case DecodeCode::BOOL: return "BOOL";
        case DecodeCode::BIT: return "BIT";
    }
    return "UINT16";
}

static DecodeCode stringToDecodeCode(const std::string& s) {
    if (s == "INT16") return DecodeCode::INT16;
    if (s == "UINT32") return DecodeCode::UINT32;
    if (s == "INT32") return DecodeCode::INT32;
    if (s == "FLOAT") return DecodeCode::FLOAT;
    if (s == "DOUBLE") return DecodeCode::DOUBLE;
    if (s == "UINT8") return DecodeCode::UINT8;
    if (s == "INT8") return DecodeCode::INT8;
    if (s == "BOOL") return DecodeCode::BOOL;
    if (s == "BIT") return DecodeCode::BIT;
    return DecodeCode::UINT16;
}

static std::string qualityToString(PointQuality q) {
    switch (q) {
        case PointQuality::Good: return "Good";
        case PointQuality::Invalid: return "Invalid";
        case PointQuality::Reserved: return "Reserved";
        case PointQuality::Error: return "Error";
    }
    return "Good";
}

static PointQuality stringToQuality(const std::string& s) {
    if (s == "Invalid") return PointQuality::Invalid;
    if (s == "Reserved") return PointQuality::Reserved;
    if (s == "Error") return PointQuality::Error;
    return PointQuality::Good;
}

static std::string ykStatusToString(YKStatus s) {
    switch (s) {
        case YKStatus::Idle: return "Idle";
        case YKStatus::Pending: return "Pending";
        case YKStatus::Executing: return "Executing";
        case YKStatus::Success: return "Success";
        case YKStatus::Failed: return "Failed";
    }
    return "Idle";
}

static YKStatus stringToYKStatus(const std::string& s) {
    if (s == "Pending") return YKStatus::Pending;
    if (s == "Executing") return YKStatus::Executing;
    if (s == "Success") return YKStatus::Success;
    if (s == "Failed") return YKStatus::Failed;
    return YKStatus::Idle;
}

std::string YcPoint::toJson() const {
    std::ostringstream ss;
    ss << "{"
       << "\"code\":\"" << escapeJson(code_) << "\","
       << "\"name\":\"" << escapeJson(name_) << "\","
       << "\"type\":\"YC\","
       << "\"decode_code\":\"" << decodeCodeToString(decodeCode_) << "\","
       << "\"value\":" << value_ << ","
       << "\"raw_value\":" << rawValue_ << ","
       << "\"mul_coe\":" << mulCoe_ << ","
       << "\"add_coe\":" << addCoe_ << ","
       << "\"min_limit\":" << minLimit_ << ","
       << "\"max_limit\":" << maxLimit_ << ","
       << "\"unit\":\"" << escapeJson(unit_) << "\","
       << "\"timestamp\":" << timestamp_ << ","
       << "\"quality\":\"" << qualityToString(quality_) << "\","
       << "\"status\":" << status_
       << "}";
    return ss.str();
}

void YcPoint::fromJson(const std::string& json) {
    auto findStr = [&](const std::string& key) -> std::string {
        auto pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        pos = json.find(":", pos + key.size() + 2);
        if (pos == std::string::npos) return "";
        pos++;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        if (pos >= json.size()) return "";
        if (json[pos] == '"') {
            pos++;
            std::string val;
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\' && pos + 1 < json.size()) {
                    pos++;
                    if (json[pos] == '"') val += '"';
                    else if (json[pos] == '\\') val += '\\';
                    else if (json[pos] == 'n') val += '\n';
                    else { val += '\\'; val += json[pos]; }
                } else {
                    val += json[pos];
                }
                pos++;
            }
            return val;
        }
        size_t end = pos;
        while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ' ') end++;
        return json.substr(pos, end - pos);
    };

    code_ = findStr("code");
    name_ = findStr("name");
    decodeCode_ = stringToDecodeCode(findStr("decode_code"));
    try {
        value_ = std::stod(findStr("value"));
        rawValue_ = std::stod(findStr("raw_value"));
        mulCoe_ = std::stod(findStr("mul_coe"));
        addCoe_ = std::stod(findStr("add_coe"));
        minLimit_ = std::stod(findStr("min_limit"));
        maxLimit_ = std::stod(findStr("max_limit"));
        timestamp_ = std::stoull(findStr("timestamp"));
        status_ = std::stoi(findStr("status"));
    } catch (...) {
    }
    quality_ = stringToQuality(findStr("quality"));
    unit_ = findStr("unit");
}

std::string YxPoint::toJson() const {
    std::ostringstream ss;
    ss << "{"
       << "\"code\":\"" << escapeJson(code_) << "\","
       << "\"name\":\"" << escapeJson(name_) << "\","
       << "\"type\":\"YX\","
       << "\"decode_code\":\"" << decodeCodeToString(decodeCode_) << "\","
       << "\"value\":" << value_ << ","
       << "\"bit\":" << bit_ << ","
       << "\"reverse\":" << (reverse_ ? "true" : "false") << ","
       << "\"timestamp\":" << timestamp_ << ","
       << "\"quality\":\"" << qualityToString(quality_) << "\","
       << "\"status\":" << status_
       << "}";
    return ss.str();
}

void YxPoint::fromJson(const std::string& json) {
    auto findStr = [&](const std::string& key) -> std::string {
        auto pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        pos = json.find(":", pos + key.size() + 2);
        if (pos == std::string::npos) return "";
        pos++;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        if (pos >= json.size()) return "";
        if (json[pos] == '"') {
            pos++;
            std::string val;
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\' && pos + 1 < json.size()) {
                    pos++;
                    if (json[pos] == '"') val += '"';
                    else if (json[pos] == '\\') val += '\\';
                    else { val += '\\'; val += json[pos]; }
                } else {
                    val += json[pos];
                }
                pos++;
            }
            return val;
        }
        size_t end = pos;
        while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ' ') end++;
        return json.substr(pos, end - pos);
    };

    code_ = findStr("code");
    name_ = findStr("name");
    decodeCode_ = stringToDecodeCode(findStr("decode_code"));
    try {
        value_ = std::stoi(findStr("value"));
        bit_ = std::stoi(findStr("bit"));
        reverse_ = (findStr("reverse") == "true");
        timestamp_ = std::stoull(findStr("timestamp"));
        status_ = std::stoi(findStr("status"));
    } catch (...) {
    }
    quality_ = stringToQuality(findStr("quality"));
}

std::string YkPoint::toJson() const {
    std::ostringstream ss;
    ss << "{"
       << "\"code\":\"" << escapeJson(code_) << "\","
       << "\"name\":\"" << escapeJson(name_) << "\","
       << "\"type\":\"YK\","
       << "\"decode_code\":\"" << decodeCodeToString(decodeCode_) << "\","
       << "\"value\":" << value_ << ","
       << "\"command_type\":" << commandType_ << ","
       << "\"yk_status\":\"" << ykStatusToString(status_) << "\","
       << "\"timestamp\":" << timestamp_ << ","
       << "\"execute_time\":" << executeTime_ << ","
       << "\"result\":\"" << escapeJson(result_) << "\""
       << "}";
    return ss.str();
}

void YkPoint::fromJson(const std::string& json) {
    auto findStr = [&](const std::string& key) -> std::string {
        auto pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        pos = json.find(":", pos + key.size() + 2);
        if (pos == std::string::npos) return "";
        pos++;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        if (pos >= json.size()) return "";
        if (json[pos] == '"') {
            pos++;
            std::string val;
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\' && pos + 1 < json.size()) {
                    pos++;
                    if (json[pos] == '"') val += '"';
                    else if (json[pos] == '\\') val += '\\';
                    else { val += '\\'; val += json[pos]; }
                } else {
                    val += json[pos];
                }
                pos++;
            }
            return val;
        }
        size_t end = pos;
        while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ' ') end++;
        return json.substr(pos, end - pos);
    };

    code_ = findStr("code");
    name_ = findStr("name");
    decodeCode_ = stringToDecodeCode(findStr("decode_code"));
    try {
        value_ = std::stoi(findStr("value"));
        commandType_ = std::stoi(findStr("command_type"));
        status_ = stringToYKStatus(findStr("yk_status"));
        timestamp_ = std::stoull(findStr("timestamp"));
        executeTime_ = std::stoull(findStr("execute_time"));
    } catch (...) {
    }
    result_ = findStr("result");
}

std::string YtPoint::toJson() const {
    std::ostringstream ss;
    ss << "{"
       << "\"code\":\"" << escapeJson(code_) << "\","
       << "\"name\":\"" << escapeJson(name_) << "\","
       << "\"type\":\"YT\","
       << "\"decode_code\":\"" << decodeCodeToString(decodeCode_) << "\","
       << "\"value\":" << value_ << ","
       << "\"mul_coe\":" << mulCoe_ << ","
       << "\"add_coe\":" << addCoe_ << ","
       << "\"min_limit\":" << minLimit_ << ","
       << "\"max_limit\":" << maxLimit_ << ","
       << "\"unit\":\"" << escapeJson(unit_) << "\","
       << "\"timestamp\":" << timestamp_ << ","
       << "\"quality\":\"" << qualityToString(quality_) << "\","
       << "\"status\":" << status_
       << "}";
    return ss.str();
}

void YtPoint::fromJson(const std::string& json) {
    auto findStr = [&](const std::string& key) -> std::string {
        auto pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        pos = json.find(":", pos + key.size() + 2);
        if (pos == std::string::npos) return "";
        pos++;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        if (pos >= json.size()) return "";
        if (json[pos] == '"') {
            pos++;
            std::string val;
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\' && pos + 1 < json.size()) {
                    pos++;
                    if (json[pos] == '"') val += '"';
                    else if (json[pos] == '\\') val += '\\';
                    else { val += '\\'; val += json[pos]; }
                } else {
                    val += json[pos];
                }
                pos++;
            }
            return val;
        }
        size_t end = pos;
        while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ' ') end++;
        return json.substr(pos, end - pos);
    };

    code_ = findStr("code");
    name_ = findStr("name");
    decodeCode_ = stringToDecodeCode(findStr("decode_code"));
    try {
        value_ = std::stod(findStr("value"));
        mulCoe_ = std::stod(findStr("mul_coe"));
        addCoe_ = std::stod(findStr("add_coe"));
        minLimit_ = std::stod(findStr("min_limit"));
        maxLimit_ = std::stod(findStr("max_limit"));
        timestamp_ = std::stoull(findStr("timestamp"));
        status_ = std::stoi(findStr("status"));
    } catch (...) {
    }
    quality_ = stringToQuality(findStr("quality"));
    unit_ = findStr("unit");
}

}
}