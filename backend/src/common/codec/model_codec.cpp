/**
 * @file model_codec.cpp
 * @brief 模型 JSON 编解码实现（nlohmann 为私有实现，不进入公开头文件）
 */
#include <common/codec/model_codec.h>

#include <cmath>
#include <nlohmann/json.hpp>

namespace ems::codec {
namespace {

using nlohmann::json;

// ---- 契约编码辅助：64 位整数/ID 统一十进制字符串 ----

json id_to_json(const std::string& value) { return json(value); }

std::string json_to_id(const json& j, const char* field) {
    if (!j.contains(field) || j[field].is_null()) return {};
    if (j[field].is_string()) return j[field].get<std::string>();
    // 数字：兼容旧数字 ID 直接写入（仍按十进制文本转出）
    return std::to_string(j[field].get<std::int64_t>());
}

json i64_to_json(std::int64_t v) { return json(std::to_string(v)); }

json u64_to_json(std::uint64_t v) { return json(std::to_string(v)); }

core::Result<std::int64_t> json_to_i64(const json& j, const char* field) {
    if (!j.contains(field) || j[field].is_null())
        return core::make_error(core::ErrorCode::SchemaMismatch, core::WriteEffect::NotApplicable,
                                std::string("缺少字段: ") + field);
    if (j[field].is_string()) {
        const std::string s = j[field].get<std::string>();
        try {
            std::size_t pos = 0;
            const std::int64_t v = std::stoll(s, &pos);
            if (pos != s.size()) throw std::invalid_argument("trailing");
            return v;
        } catch (...) {
            return core::make_error(core::ErrorCode::SchemaMismatch,
                                    core::WriteEffect::NotApplicable,
                                    std::string(field) + " 不是合法十进制字符串: " + s);
        }
    }
    if (j[field].is_number_integer()) return j[field].get<std::int64_t>();
    return core::make_error(core::ErrorCode::SchemaMismatch, core::WriteEffect::NotApplicable,
                            std::string(field) + " 必须是十进制字符串");
}

std::string require_str(const json& j, const char* field, bool& ok) {
    if (!j.contains(field) || !j[field].is_string()) {
        ok = false;
        return {};
    }
    return j[field].get<std::string>();
}

bool get_bool(const json& j, const char* field, bool default_value) {
    if (!j.contains(field) || !j[field].is_boolean()) return default_value;
    return j[field].get<bool>();
}

double get_double(const json& j, const char* field, double default_value) {
    if (!j.contains(field) || !j[field].is_number()) return default_value;
    return j[field].get<double>();
}

core::Error schema_error(const std::string& msg) {
    return core::make_error(core::ErrorCode::SchemaMismatch, core::WriteEffect::NotApplicable, msg);
}

core::Error argument_error(const std::string& msg) {
    return core::make_error(core::ErrorCode::InvalidArgument, core::WriteEffect::NotApplicable,
                            msg);
}

// ---- ValueType / PointKind 枚举映射（契约码表）----

const char* value_type_name(model::ValueType t) { return model::to_string(t); }

core::Result<model::ValueType> parse_value_type(const std::string& s) {
    if (s == "Bool") return model::ValueType::Bool;
    if (s == "Int64") return model::ValueType::Int64;
    if (s == "UInt64") return model::ValueType::UInt64;
    if (s == "Double") return model::ValueType::Double;
    if (s == "String") return model::ValueType::String;
    return schema_error("未知 ValueType: " + s);
}

const char* point_kind_name(model::PointKind k) { return model::to_string(k); }

core::Result<model::PointKind> parse_point_kind(const std::string& s) {
    if (s == "Telemetry") return model::PointKind::Telemetry;
    if (s == "Status") return model::PointKind::Status;
    if (s == "Event") return model::PointKind::Event;
    if (s == "Command") return model::PointKind::Command;
    return schema_error("未知 PointKind: " + s);
}

}  // namespace

// ---- PointValue ----

core::Result<std::string> encode_point_value(model::ValueType type,
                                             const model::PointValue& value) {
    if (!model::value_matches_type(value, type)) {
        return argument_error("值与声明类型不匹配: " + std::string(value_type_name(type)));
    }
    json j;
    switch (type) {
        case model::ValueType::Bool:
            j = std::get<bool>(value);
            break;
        case model::ValueType::Int64:
            j = std::to_string(std::get<std::int64_t>(value));  // 十进制字符串
            break;
        case model::ValueType::UInt64:
            j = std::to_string(std::get<std::uint64_t>(value));
            break;
        case model::ValueType::Double: {
            const double d = std::get<double>(value);
            if (!std::isfinite(d)) {
                return argument_error("Double 值禁止 NaN/Inf");
            }
            j = d;  // double 保留 JSON number
            break;
        }
        case model::ValueType::String:
            j = std::get<std::string>(value);
            break;
    }
    return j.dump();
}

core::Result<model::PointValue> decode_point_value(model::ValueType type,
                                                   const std::string& json_text) {
    json j = json::parse(json_text, nullptr, false);
    if (j.is_discarded()) {
        return schema_error("PointValue JSON 解析失败");
    }
    switch (type) {
        case model::ValueType::Bool:
            if (!j.is_boolean()) return schema_error("期望 Bool");
            return model::PointValue{j.get<bool>()};
        case model::ValueType::Int64: {
            if (j.is_number_integer()) return model::PointValue{j.get<std::int64_t>()};
            if (!j.is_string()) return schema_error("期望 Int64 十进制字符串");
            auto v = json_to_i64(json{{"v", j}}, "v");
            if (!core::is_ok(v))
                return core::error_of(v).message.empty() ? schema_error("期望 Int64 十进制字符串")
                                                         : core::error_of(v);
            return model::PointValue{core::value_of(v)};
        }
        case model::ValueType::UInt64: {
            if (j.is_number_unsigned()) return model::PointValue{j.get<std::uint64_t>()};
            if (!j.is_string()) return schema_error("期望 UInt64 十进制字符串");
            const std::string s = j.get<std::string>();
            try {
                std::size_t pos = 0;
                const std::uint64_t v = std::stoull(s, &pos);
                if (pos != s.size()) throw std::invalid_argument("trailing");
                return model::PointValue{v};
            } catch (...) {
                return schema_error("非法 UInt64 十进制字符串: " + s);
            }
        }
        case model::ValueType::Double:
            if (!j.is_number()) return schema_error("期望 Double");
            return model::PointValue{j.get<double>()};
        case model::ValueType::String:
            if (!j.is_string()) return schema_error("期望 String");
            return model::PointValue{j.get<std::string>()};
    }
    return schema_error("未知 ValueType");
}

// ---- AssetNode ----

core::Result<std::string> encode_asset(const model::AssetNode& a) {
    json j;
    j["asset_id"] = id_to_json(a.asset_id.value());
    j["station_id"] = id_to_json(a.station_id.value());
    j["parent_asset_id"] = a.parent_asset_id.has_value()
                               ? json(id_to_json(a.parent_asset_id->value()))
                               : json(nullptr);
    j["code"] = a.code;
    j["name"] = a.name;
    j["asset_type"] = static_cast<int>(a.asset_type);
    return j.dump();
}

core::Result<model::AssetNode> decode_asset(const std::string& json_text) {
    json j = json::parse(json_text, nullptr, false);
    if (j.is_discarded()) return schema_error("AssetNode JSON 解析失败");
    bool ok = true;
    model::AssetNode a;
    a.asset_id = core::AssetId(require_str(j, "asset_id", ok));
    a.station_id = core::StationId(require_str(j, "station_id", ok));
    if (!ok) return schema_error("AssetNode 缺少必需字段 asset_id/station_id");
    if (j.contains("parent_asset_id") && !j["parent_asset_id"].is_null()) {
        a.parent_asset_id = core::AssetId(json_to_id(j, "parent_asset_id"));
    }
    a.code = j.value("code", std::string{});
    a.name = j.value("name", std::string{});
    if (j.contains("asset_type") && j["asset_type"].is_number_integer()) {
        a.asset_type = static_cast<model::AssetType>(j["asset_type"].get<int>());
    }
    return a;
}

// ---- Device ----

core::Result<std::string> encode_device(const model::Device& d) {
    json j;
    j["device_id"] = id_to_json(d.device_id.value());
    j["asset_id"] = id_to_json(d.asset_id.value());
    j["code"] = d.code;
    j["name"] = d.name;
    j["type_version_id"] = d.type_version_id;
    j["model_version_id"] = d.model_version_id;
    j["vendor"] = d.vendor;
    j["firmware_version"] = d.firmware_version;
    j["enabled"] = d.enabled;
    return j.dump();
}

core::Result<model::Device> decode_device(const std::string& json_text) {
    json j = json::parse(json_text, nullptr, false);
    if (j.is_discarded()) return schema_error("Device JSON 解析失败");
    bool ok = true;
    model::Device d;
    d.device_id = core::DeviceId(require_str(j, "device_id", ok));
    d.asset_id = core::AssetId(require_str(j, "asset_id", ok));
    if (!ok) return schema_error("Device 缺少必需字段 device_id/asset_id");
    d.code = j.value("code", std::string{});
    d.name = j.value("name", std::string{});
    d.type_version_id = j.value("type_version_id", std::string{});
    d.model_version_id = j.value("model_version_id", std::string{});
    d.vendor = j.value("vendor", std::string{});
    d.firmware_version = j.value("firmware_version", std::string{});
    d.enabled = get_bool(j, "enabled", true);
    return d;
}

// ---- Point ----

core::Result<std::string> encode_point(const model::Point& p) {
    json j;
    j["point_id"] = id_to_json(p.point_id.value());
    j["asset_id"] = id_to_json(p.asset_id.value());
    j["code"] = p.code;
    j["name"] = p.name;
    j["kind"] = point_kind_name(p.kind);
    j["value_type"] = value_type_name(p.value_type);
    j["unit"] = p.unit;
    j["enabled"] = p.enabled;
    if (p.feedback_of.has_value()) {
        j["feedback_of"] = id_to_json(p.feedback_of->value());
    }
    return j.dump();
}

core::Result<model::Point> decode_point(const std::string& json_text) {
    json j = json::parse(json_text, nullptr, false);
    if (j.is_discarded()) return schema_error("Point JSON 解析失败");
    bool ok = true;
    model::Point p;
    p.point_id = core::PointId(require_str(j, "point_id", ok));
    p.asset_id = core::AssetId(require_str(j, "asset_id", ok));
    if (!ok) return schema_error("Point 缺少必需字段 point_id/asset_id");
    p.code = j.value("code", std::string{});
    p.name = j.value("name", std::string{});
    if (j.contains("kind") && j["kind"].is_string()) {
        auto k = parse_point_kind(j["kind"].get<std::string>());
        if (!core::is_ok(k)) return core::error_of(k);
        p.kind = core::value_of(k);
    }
    if (j.contains("value_type") && j["value_type"].is_string()) {
        auto t = parse_value_type(j["value_type"].get<std::string>());
        if (!core::is_ok(t)) return core::error_of(t);
        p.value_type = core::value_of(t);
    }
    p.unit = j.value("unit", std::string{});
    p.enabled = get_bool(j, "enabled", true);
    if (j.contains("feedback_of") && !j["feedback_of"].is_null()) {
        p.feedback_of = core::PointId(json_to_id(j, "feedback_of"));
    }
    return p;
}

// ---- DeviceChannelBinding ----

core::Result<std::string> encode_binding(const model::DeviceChannelBinding& b) {
    json j;
    j["binding_id"] = b.binding_id;
    j["device_id"] = id_to_json(b.device_id.value());
    j["channel_id"] = id_to_json(b.channel_id.value());
    json ops = json::array();
    for (auto op : b.operations) ops.push_back(model::to_string(op));
    j["operations"] = ops;
    j["protocol_address"] = b.protocol_address;
    j["version"] = b.version;
    j["enabled"] = b.enabled;
    return j.dump();
}

core::Result<model::DeviceChannelBinding> decode_binding(const std::string& json_text) {
    json j = json::parse(json_text, nullptr, false);
    if (j.is_discarded()) return schema_error("DeviceChannelBinding JSON 解析失败");
    bool ok = true;
    model::DeviceChannelBinding b;
    b.binding_id = require_str(j, "binding_id", ok);
    b.device_id = core::DeviceId(require_str(j, "device_id", ok));
    b.channel_id = core::ChannelId(require_str(j, "channel_id", ok));
    if (!ok) return schema_error("DeviceChannelBinding 缺少必需字段");
    if (j.contains("operations") && j["operations"].is_array()) {
        for (const auto& op : j["operations"]) {
            if (!op.is_string()) return schema_error("operations 元素必须是字符串");
            const std::string s = op.get<std::string>();
            if (s == "Collect")
                b.operations.push_back(model::BindingOp::Collect);
            else if (s == "Control")
                b.operations.push_back(model::BindingOp::Control);
            else if (s == "Forward")
                b.operations.push_back(model::BindingOp::Forward);
            else
                return schema_error("未知 BindingOp: " + s);
        }
    }
    b.protocol_address = j.value("protocol_address", std::string{});
    b.version = j.value("version", std::string{});
    b.enabled = get_bool(j, "enabled", true);
    return b;
}

// ---- VendorPointRoute ----

core::Result<std::string> encode_vendor_route(const model::VendorPointRoute& r) {
    if (!std::isfinite(r.scale) || !std::isfinite(r.offset) || r.address_offset > 65535) {
        return argument_error("VendorPointRoute 转换系数或地址偏移非法");
    }
    json j;
    j["route_id"] = r.route_id;
    j["point_id"] = id_to_json(r.point_id.value());
    j["binding_id"] = r.binding_id;
    j["vendor_address"] = r.vendor_address;
    j["template_key"] = r.template_key;
    j["vendor_name"] = r.vendor_name;
    j["protocol"] = r.protocol;
    j["address_space"] = r.address_space;
    j["address_offset"] = r.address_offset;
    j["wire_type"] = r.wire_type;
    j["byte_order"] = r.byte_order;
    j["scale"] = r.scale;
    j["offset"] = r.offset;
    j["writable"] = r.writable;
    j["enabled"] = r.enabled;
    return j.dump();
}

core::Result<model::VendorPointRoute> decode_vendor_route(const std::string& json_text) {
    json j = json::parse(json_text, nullptr, false);
    if (j.is_discarded()) return schema_error("VendorPointRoute JSON 解析失败");
    bool ok = true;
    model::VendorPointRoute r;
    r.route_id = require_str(j, "route_id", ok);
    r.point_id = core::PointId(require_str(j, "point_id", ok));
    r.binding_id = require_str(j, "binding_id", ok);
    if (!ok) return schema_error("VendorPointRoute 缺少必需字段");
    if ((j.contains("scale") && !j["scale"].is_number()) ||
        (j.contains("offset") && !j["offset"].is_number()) ||
        (j.contains("writable") && !j["writable"].is_boolean()) ||
        (j.contains("enabled") && !j["enabled"].is_boolean())) {
        return schema_error("VendorPointRoute 字段类型非法");
    }
    try {
        r.vendor_address = j.value("vendor_address", std::string{});
        r.template_key = j.value("template_key", std::string{});
        r.vendor_name = j.value("vendor_name", std::string{});
        r.protocol = j.value("protocol", std::string{});
        r.address_space = j.value("address_space", std::string{});
        r.address_offset = j.value("address_offset", std::uint32_t{0});
        r.wire_type = j.value("wire_type", std::string{});
        r.byte_order = j.value("byte_order", std::string{});
        r.scale = get_double(j, "scale", 1.0);
        r.offset = get_double(j, "offset", 0.0);
        r.writable = get_bool(j, "writable", false);
        r.enabled = get_bool(j, "enabled", true);
    } catch (...) {
        return schema_error("VendorPointRoute 字段类型非法");
    }
    if (!std::isfinite(r.scale) || !std::isfinite(r.offset) || r.address_offset > 65535) {
        return schema_error("VendorPointRoute 转换系数或地址偏移非法");
    }
    return r;
}

// ---- ForwardingPointMap ----

core::Result<std::string> encode_forwarding_map(const model::ForwardingPointMap& m) {
    json j;
    j["map_id"] = m.map_id;
    j["point_id"] = id_to_json(m.point_id.value());
    j["north_address"] = m.north_address;
    const char* dir = "Upstream";
    if (m.direction == model::ForwardDirection::Downstream)
        dir = "Downstream";
    else if (m.direction == model::ForwardDirection::Bidirectional)
        dir = "Bidirectional";
    j["direction"] = dir;
    j["enabled"] = m.enabled;
    return j.dump();
}

core::Result<model::ForwardingPointMap> decode_forwarding_map(const std::string& json_text) {
    json j = json::parse(json_text, nullptr, false);
    if (j.is_discarded()) return schema_error("ForwardingPointMap JSON 解析失败");
    bool ok = true;
    model::ForwardingPointMap m;
    m.map_id = require_str(j, "map_id", ok);
    m.point_id = core::PointId(require_str(j, "point_id", ok));
    if (!ok) return schema_error("ForwardingPointMap 缺少必需字段");
    m.north_address = j.value("north_address", std::string{});
    if (j.contains("direction") && j["direction"].is_string()) {
        const std::string s = j["direction"].get<std::string>();
        if (s == "Upstream")
            m.direction = model::ForwardDirection::Upstream;
        else if (s == "Downstream")
            m.direction = model::ForwardDirection::Downstream;
        else if (s == "Bidirectional")
            m.direction = model::ForwardDirection::Bidirectional;
        else
            return schema_error("未知 ForwardDirection: " + s);
    }
    m.enabled = get_bool(j, "enabled", true);
    return m;
}

}  // namespace ems::codec
