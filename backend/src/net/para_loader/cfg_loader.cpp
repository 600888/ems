#include "cfg_loader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

namespace ems::net::device {
namespace {
using json = nlohmann::json;
using namespace ems::database;

json object(const std::string& text) {
    if (text.empty() || text == "null") return json::object();
    auto result = json::parse(text);
    if (!result.is_object())
        throw std::invalid_argument("protocol/serial parameters must be objects");
    return result;
}

int number(const std::string& text, int minimum, int maximum) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("binding device address must be an integer");
    const auto value = std::stoll(text);
    if (value < minimum || value > maximum)
        throw std::invalid_argument("binding address out of range");
    return static_cast<int>(value);
}

ems::model::ProtocolType protocol(const CfgChannelRow& channel) {
    using P = ems::model::ProtocolType;
    if (channel.protocol == "modbus")
        return channel.conn_mode == "serial" ? P::ModbusRTU : P::ModbusTCP;
    auto name = channel.protocol;
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::map<std::string, P> protocols{
        {"modbustcp", P::ModbusTCP},  {"modbus_tcp", P::ModbusTCP}, {"modbusrtu", P::ModbusRTU},
        {"modbus_rtu", P::ModbusRTU}, {"iec104", P::IEC104},        {"dlt645", P::DLT645},
        {"dlt698", P::DLT698}};
    auto parsed = protocols.find(name);
    if (parsed != protocols.end()) return parsed->second;
    throw std::invalid_argument("collector does not support configured protocol");
}

ems::rtdb::ChannelConfig compile_channel(const CfgChannelRow& channel) {
    if (channel.id <= 0 || channel.endpoint.empty() || channel.timeout_ms <= 0 ||
        channel.poll_interval_ms <= 0 || channel.retry_times < 0 || channel.max_sessions < 1)
        throw std::invalid_argument("invalid channel endpoint, identity or timing");
    if (channel.conn_mode != "tcp_client" && channel.conn_mode != "tcp_server" &&
        channel.conn_mode != "serial")
        throw std::invalid_argument("collector channel mode is unsupported");
    ems::rtdb::ChannelConfig result;
    result.id_ = channel.id;
    result.name_ = channel.name;
    result.protocol_ = protocol(channel);
    result.connection_ = channel.endpoint;
    result.status_ = channel.enabled;
    auto settings = object(channel.serial_json);
    result.baudRate_ = settings.value("baud_rate", settings.value("baudrate", 9600));
    result.dataBits_ = settings.value("data_bits", 8);
    result.stopBits_ = settings.value("stop_bits", 1);
    result.parity_ = settings.value("parity", std::string("N"));
    if (result.parity_ == "none") result.parity_ = "N";
    if (result.parity_ == "even") result.parity_ = "E";
    if (result.parity_ == "odd") result.parity_ = "O";
    settings["timeout_ms"] = channel.timeout_ms;
    settings["poll_interval_ms"] = channel.poll_interval_ms;
    settings["retry_times"] = channel.retry_times;
    settings["max_sessions"] = channel.max_sessions;
    settings["local_role"] = channel.conn_mode == "tcp_server" ? "server" : "client";
    settings["transport"] = channel.conn_mode == "serial" ? "rtu" : "tcp";
    result.settings_ = settings.dump();
    return result;
}

template <class Point>
std::string address(const Point& point, const CfgBindingRow& binding, const CfgChannelRow& channel,
                    bool write) {
    const auto extra = object(point.protocol_address_json);
    using P = ems::model::ProtocolType;
    const auto kind = protocol(channel);
    if (kind == P::ModbusTCP || kind == P::ModbusRTU) {
        const int slave =
            number(binding.device_address.empty() ? "1" : binding.device_address, 1, 247);
        const std::map<std::string, int> reads{
            {"coil", 1}, {"discrete_input", 2}, {"holding_register", 3}, {"input_register", 4}};
        auto found = reads.find(point.address_space);
        if (found == reads.end()) throw std::invalid_argument("invalid Modbus address space");
        int function = found->second;
        if (write) {
            if (function != 1 && function != 3)
                throw std::invalid_argument("Modbus point is not writable");
            function = function == 1 ? 5 : (point.register_count > 1 ? 16 : 6);
        }
        const std::map<std::string, int> decodes{{"u16", 0}, {"i16", 1}, {"u32", 2}, {"i32", 3},
                                                 {"f32", 4}, {"f64", 5}, {"bool", 8}};
        auto decoder = decodes.find(point.wire_type);
        if (decoder == decodes.end())
            throw std::invalid_argument("Modbus wire type is unsupported by the protocol plugin");
        int decode = decoder->second;
        if (point.bit_offset > 0 ||
            (point.wire_type == "bool" && (found->second == 3 || found->second == 4)))
            decode = 9;
        const int endian = extra.value("endian", point.byte_order == "ba" ? 2 : 0);
        if (endian < 0 || endian > 3 || point.address_offset < 0 || point.address_offset > 65535 ||
            point.bit_offset < 0 || point.bit_offset > 15 || point.register_count < 1 ||
            point.register_count > 4)
            throw std::invalid_argument("Modbus point address/layout out of range");
        const int width = decode == 5 ? 4 : (decode >= 2 && decode <= 4 ? 2 : 1);
        if (point.address_offset + width > 65536 || point.register_count != width)
            throw std::invalid_argument("register count does not match wire type");
        std::string result = std::to_string(slave) + ":" + std::to_string(function) + ":" +
                             std::to_string(point.address_offset) + ":" + std::to_string(decode) +
                             ":" + std::to_string(endian);
        if (decode == 9) result += ":" + std::to_string(point.bit_offset);
        return result;
    }
    if (kind == P::IEC104) {
        const int ca =
            extra.contains("casdu")
                ? extra.at("casdu").get<int>()
                : number(binding.device_address.empty() ? "1" : binding.device_address, 1, 65535);
        const int ioa = extra.at("ioa").template get<int>();
        int type = std::is_same_v<Point, CfgPointYcRow>
                       ? 13
                       : (std::is_same_v<Point, CfgPointYxRow>
                              ? 1
                              : (std::is_same_v<Point, CfgPointYkRow> ? 45 : 50));
        const std::map<std::string, int> types{
            {"M_ME_NA_1", 9},  {"M_ME_NB_1", 11}, {"M_ME_NC_1", 13}, {"M_ME_TD_1", 34},
            {"M_ME_TE_1", 35}, {"M_ME_TF_1", 36}, {"M_SP_NA_1", 1},  {"M_DP_NA_1", 3},
            {"M_SP_TB_1", 30}, {"M_DP_TB_1", 31}, {"C_SC_NA_1", 45}, {"C_DC_NA_1", 46},
            {"C_SE_NA_1", 48}, {"C_SE_NB_1", 49}, {"C_SE_NC_1", 50}};
        if (extra.contains("iec104_type"))
            type = types.at(extra.at("iec104_type").template get<std::string>());
        else
            type = extra.value("type_id", type);
        if (ca < 1 || ca > 65535 || ioa < 0 || ioa > 0xffffff || type < 1 || type > 255)
            throw std::invalid_argument("IEC104 address out of range");
        return std::to_string(ca) + ":" + std::to_string(ioa) + ":" + std::to_string(type);
    }
    if (kind == P::DLT645) {
        if constexpr (!std::is_same_v<Point, CfgPointYcRow>)
            throw std::invalid_argument("DLT645 collection currently supports YC only");
        std::string di;
        if (extra.at("data_id").is_string())
            di = extra.at("data_id").template get<std::string>();
        else
            di = std::to_string(extra.at("data_id").template get<std::uint32_t>());
        if (!binding.device_address.empty() &&
            (binding.device_address.size() != 12 ||
             binding.device_address.find_first_not_of("0123456789") != std::string::npos))
            throw std::invalid_argument("DLT645 meter address must contain 12 digits");
        return (binding.device_address.empty() ? "" : binding.device_address + ":") + di;
    }
    if (kind == P::DLT698) return extra.at("oad").template get<std::string>();
    throw std::invalid_argument("unsupported point protocol");
}
}  // namespace

bool CfgLoader::initialize(ems::database::MySqlDatabaseContext database,
                           const ems::core::CallContext& ctx) {
    if (!database.database || !database.device_config) {
        error_ = "device configuration context is incomplete";
        return false;
    }
    database_ = std::move(database);
    return reload(ctx);
}

bool CfgLoader::reload(const ems::core::CallContext& ctx) {
    if (!database_.database || !database_.device_config) return false;
    auto begun = database_.database->begin(ctx);
    if (!ems::core::is_ok(begun)) {
        error_ = ems::core::error_of(begun).message;
        return false;
    }
    auto tx = std::move(std::get<0>(begun));
    auto records = database_.device_config->load_configuration(*tx, ctx);
    if (!ems::core::is_ok(records)) {
        error_ = ems::core::error_of(records).message;
        return false;
    }
    auto closed = tx->rollback(ctx);
    if (!ems::core::is_ok(closed)) {
        error_ = ems::core::error_of(closed).message;
        return false;
    }
    return configure(ems::core::value_of(records));
}

bool CfgLoader::configure(const ems::database::DeviceConfigRecords& records) {
    try {
        std::map<std::string, const CfgDeviceRow*> nodes;
        std::map<std::string, const CfgChannelRow*> channels;
        std::map<std::string, const CfgBindingRow*> bindings;
        std::map<std::int64_t, ems::rtdb::ChannelConfig> compiled;
        std::map<std::int64_t, std::map<std::string, ConfiguredDevice>> routes;
        std::int64_t revision = 0;
        std::size_t roots = 0;
        for (const auto& node : records.devices) {
            if (node.device_id.empty() || node.id <= 0 ||
                !nodes.emplace(node.device_id, &node).second)
                throw std::invalid_argument("device identity is invalid or duplicated");
            if (node.node_kind == "station") {
                revision = node.config_revision;
                ++roots;
            }
        }
        if (roots > 1) throw std::invalid_argument("more than one station root");
        auto enabled = [&](const std::string& id) {
            auto cursor = nodes.find(id);
            std::set<std::string> visited;
            while (cursor != nodes.end()) {
                const auto& node = *cursor->second;
                if (!visited.insert(node.device_id).second)
                    throw std::invalid_argument("device parent cycle");
                if (!node.enabled) return false;
                if (node.parent_device_id.empty()) {
                    if (node.node_kind != "station")
                        throw std::invalid_argument("device tree must start at a station");
                    return true;
                }
                cursor = nodes.find(node.parent_device_id);
            }
            throw std::invalid_argument("device or parent is missing");
        };
        for (const auto& channel : records.channels)
            if (channel.channel_id.empty() ||
                !channels.emplace(channel.channel_id, &channel).second)
                throw std::invalid_argument("channel identity is invalid or duplicated");
        for (const auto& binding : records.bindings) {
            if (binding.binding_id.empty() ||
                !bindings.emplace(binding.binding_id, &binding).second)
                throw std::invalid_argument("binding identity is invalid or duplicated");
            if (!binding.enabled || (!binding.can_collect && !binding.can_control)) continue;
            if (!enabled(binding.device_id)) continue;
            if (nodes.at(binding.device_id)->node_kind != "device")
                throw std::invalid_argument("binding source must be a communication device");
            auto channel = channels.find(binding.channel_id);
            if (channel == channels.end()) throw std::invalid_argument("binding channel not found");
            if (channel->second->enabled) {
                auto [entry, inserted] =
                    compiled.emplace(channel->second->id, compile_channel(*channel->second));
                if (protocol(*channel->second) == ems::model::ProtocolType::DLT698) {
                    const auto& meter = binding.device_address;
                    if (meter.empty() || meter.size() > 32 ||
                        meter.find_first_not_of("0123456789") != std::string::npos ||
                        (meter.size() > 12 && meter.size() % 2 != 0))
                        throw std::invalid_argument("invalid DLT698 binding meter address");
                    const auto normalized =
                        meter.size() < 12 ? std::string(12 - meter.size(), '0') + meter : meter;
                    auto settings = object(entry->second.settings_);
                    if (settings.contains("meter_address") &&
                        settings["meter_address"] != normalized)
                        throw std::invalid_argument(
                            "DLT698 plugin requires one meter address per channel");
                    settings["meter_address"] = normalized;
                    entry->second.settings_ = settings.dump();
                }
            }
        }
        std::set<std::string> point_ids;
        auto append = [&](const auto& points) {
            for (const auto& point : points) {
                if (point.point_id.empty() || !point_ids.insert(point.point_id).second)
                    throw std::invalid_argument("point identity is invalid or duplicated");
                if (!point.enabled || !enabled(point.owner_device_id)) continue;
                const auto& owner = *nodes.at(point.owner_device_id);
                auto found = bindings.find(point.binding_id);
                if (found == bindings.end()) throw std::invalid_argument("point binding not found");
                const auto& binding = *found->second;
                if (!binding.enabled || !enabled(binding.device_id)) continue;
                const auto& channel = *channels.at(binding.channel_id);
                if (!channel.enabled) continue;
                const auto source =
                    owner.node_kind == "component" ? owner.source_device_id : owner.device_id;
                if ((owner.node_kind != "device" && owner.node_kind != "component") ||
                    source != binding.device_id)
                    throw std::invalid_argument("point source does not match its owner/component");
                using Point = std::decay_t<decltype(point)>;
                constexpr bool write =
                    std::is_same_v<Point, CfgPointYkRow> || std::is_same_v<Point, CfgPointYtRow>;
                if ((write && !binding.can_control) || (!write && !binding.can_collect))
                    throw std::invalid_argument(
                        "point binding role does not permit this point type");
                PointRoute route;
                route.point_id = point.point_id;
                route.protocol_code = address(point, binding, channel, write);
                if constexpr (std::is_same_v<Point, CfgPointYcRow> ||
                              std::is_same_v<Point, CfgPointYtRow>) {
                    if (!std::isfinite(point.scale_factor) || point.scale_factor == 0 ||
                        !std::isfinite(point.offset_value))
                        throw std::invalid_argument("invalid point conversion");
                    route.scale_factor = point.scale_factor;
                    route.offset_value = point.offset_value;
                }
                if constexpr (std::is_same_v<Point, CfgPointYxRow>)
                    route.invert = point.invert != 0;
                if constexpr (std::is_same_v<Point, CfgPointYkRow>) {
                    route.write_mode = point.write_mode;
                    route.fixed_raw_value = static_cast<double>(point.fixed_raw_value);
                }
                if constexpr (std::is_same_v<Point, CfgPointYtRow>) route.write_mode = "scaled";
                auto& device = routes[channel.id][owner.device_id];
                device.deviceId_ = owner.id;
                device.deviceKey_ = owner.device_id;
                device.deviceName_ = owner.name;
                if constexpr (std::is_same_v<Point, CfgPointYcRow>)
                    device.ycPoints_.push_back(std::move(route));
                if constexpr (std::is_same_v<Point, CfgPointYxRow>)
                    device.yxPoints_.push_back(std::move(route));
                if constexpr (std::is_same_v<Point, CfgPointYkRow>)
                    device.ykPoints_.push_back(std::move(route));
                if constexpr (std::is_same_v<Point, CfgPointYtRow>)
                    device.ytPoints_.push_back(std::move(route));
            }
        };
        append(records.points_yc);
        append(records.points_yx);
        append(records.points_yk);
        append(records.points_yt);
        std::vector<ems::rtdb::ChannelConfig> new_channels;
        std::map<std::int64_t, std::vector<ConfiguredDevice>> new_devices;
        for (auto& [_, channel] : compiled) new_channels.push_back(std::move(channel));
        for (auto& [id, devices] : routes)
            for (auto& [_, device] : devices) new_devices[id].push_back(std::move(device));
        channels_ = std::move(new_channels);
        devices_ = std::move(new_devices);
        revision_ = revision;
        initialized_ = true;
        error_.clear();
        return true;
    } catch (const std::exception& error) {
        error_ = error.what();
        return false;
    }
}

std::vector<ConfiguredDevice> CfgLoader::devices(std::int64_t channel_id) const {
    auto found = devices_.find(channel_id);
    return found == devices_.end() ? std::vector<ConfiguredDevice>{} : found->second;
}
}  // namespace ems::net::device
