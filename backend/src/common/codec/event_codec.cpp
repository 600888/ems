/**
 * @file event_codec.cpp
 * @brief 事件信封 JSON 编解码实现（design.md 第 10.1 节）
 */
#include <common/codec/event_codec.h>

#include <nlohmann/json.hpp>

namespace ems::codec {
namespace {

using nlohmann::json;

constexpr const char* kCurrentSchemaVersion = "1";

core::Error schema_error(const std::string& msg) {
    return core::make_error(core::ErrorCode::SchemaMismatch, core::WriteEffect::NotApplicable, msg);
}

core::Result<messaging::EventType> parse_event_type(const std::string& s) {
    if (s == "Telemetry") return messaging::EventType::Telemetry;
    if (s == "ControlIntent") return messaging::EventType::ControlIntent;
    if (s == "Command") return messaging::EventType::Command;
    if (s == "CommandResult") return messaging::EventType::CommandResult;
    if (s == "AlarmEvent") return messaging::EventType::AlarmEvent;
    if (s == "ConfigChanged") return messaging::EventType::ConfigChanged;
    if (s == "ConfigApplied") return messaging::EventType::ConfigApplied;
    return schema_error("未知事件类型: " + s);
}

}  // namespace

core::Result<std::string> encode_event(const messaging::EventEnvelope& e) {
    if (e.event_id.empty()) {
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplicable,
                                "event_id 不能为空（持久受理前生成）");
    }
    json j;
    j["schema_version"] =
        e.schema_version.empty() ? std::string(kCurrentSchemaVersion) : e.schema_version;
    j["event_type"] = messaging::to_string(e.event_type);
    j["event_id"] = e.event_id.value();
    j["trace_id"] = e.trace_id;
    j["producer_id"] = e.producer_id;
    j["station_id"] = e.station_id.has_value() ? json(e.station_id->value()) : json(nullptr);
    j["unit_id"] = e.unit_id.has_value() ? json(e.unit_id->value()) : json(nullptr);
    j["device_id"] = e.device_id.has_value() ? json(e.device_id->value()) : json(nullptr);
    json device_path = json::array();
    for (const auto& p : e.device_path) device_path.push_back(p);
    j["device_path"] = device_path;  // 根事件为 []
    j["config_version"] = e.config_version;
    // 业务时间为 UTC Unix 微秒，64 位整数编码为十进制字符串
    j["event_time"] = std::to_string(e.event_time);
    j["ingest_time"] = std::to_string(e.ingest_time);
    j["payload"] = json::parse(e.payload_json.empty() ? "{}" : e.payload_json, nullptr, false);
    if (j["payload"].is_discarded()) {
        return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplicable,
                                "payload 不是合法 JSON");
    }
    return j.dump();
}

core::Result<messaging::EventEnvelope> decode_event(const std::string& json_text) {
    json j = json::parse(json_text, nullptr, false);
    if (j.is_discarded()) return schema_error("EventEnvelope JSON 解析失败");
    if (!j.contains("schema_version") || !j["schema_version"].is_string()) {
        return schema_error("缺少 schema_version");
    }
    const std::string schema_version = j["schema_version"].get<std::string>();
    if (schema_version != kCurrentSchemaVersion) {
        return schema_error("不支持的 schema_version: " + schema_version);
    }
    if (!j.contains("event_type") || !j["event_type"].is_string()) {
        return schema_error("缺少 event_type");
    }
    auto type = parse_event_type(j["event_type"].get<std::string>());
    if (!core::is_ok(type)) return core::error_of(type);
    if (!j.contains("event_id") || !j["event_id"].is_string() ||
        j["event_id"].get<std::string>().empty()) {
        return schema_error("缺少 event_id");
    }

    messaging::EventEnvelope e;
    e.schema_version = schema_version;
    e.event_type = core::value_of(type);
    e.event_id = core::EventId(j["event_id"].get<std::string>());
    e.trace_id = j.value("trace_id", std::string{});
    e.producer_id = j.value("producer_id", std::string{});
    if (j.contains("station_id") && j["station_id"].is_string()) {
        e.station_id = core::StationId(j["station_id"].get<std::string>());
    }
    if (j.contains("unit_id") && j["unit_id"].is_string()) {
        e.unit_id = core::EssUnitId(j["unit_id"].get<std::string>());
    }
    if (j.contains("device_id") && j["device_id"].is_string()) {
        e.device_id = core::DeviceId(j["device_id"].get<std::string>());
    }
    if (j.contains("device_path") && j["device_path"].is_array()) {
        for (const auto& p : j["device_path"]) {
            if (!p.is_string()) return schema_error("device_path 元素必须是字符串");
            e.device_path.push_back(p.get<std::string>());
        }
    }
    e.config_version = j.value("config_version", std::string{});
    if (j.contains("event_time")) {
        if (j["event_time"].is_string()) {
            try {
                e.event_time = std::stoll(j["event_time"].get<std::string>());
            } catch (...) {
                return schema_error("event_time 不是合法十进制字符串");
            }
        } else if (j["event_time"].is_number_integer()) {
            e.event_time = j["event_time"].get<std::int64_t>();
        }
    }
    if (j.contains("ingest_time")) {
        if (j["ingest_time"].is_string()) {
            try {
                e.ingest_time = std::stoll(j["ingest_time"].get<std::string>());
            } catch (...) {
                return schema_error("ingest_time 不是合法十进制字符串");
            }
        } else if (j["ingest_time"].is_number_integer()) {
            e.ingest_time = j["ingest_time"].get<std::int64_t>();
        }
    }
    if (j.contains("payload") && !j["payload"].is_null()) {
        e.payload_json = j["payload"].dump();
    }
    return e;
}

}  // namespace ems::codec
