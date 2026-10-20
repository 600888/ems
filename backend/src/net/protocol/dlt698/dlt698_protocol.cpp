#include "net/protocol/dlt698_protocol.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <dlt698/app/client.hpp>
#include <dlt698/app/server.hpp>
#include <dlt698/codec/data_codec.hpp>
#include <limits>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <type_traits>

#include "dlt698_join_queue.h"
#include "net/protocol/protocol_types.h"

namespace ems::net::protocol {
namespace {
using Json = nlohmann::json;
namespace model = ::dlt698::model;
namespace app = ::dlt698::app;
using Data = model::Data;
using Oad = model::Oad;
using ObjectValue = ::dlt698::service::ObjectValue;
std::mutex lifetime_mutex;
std::size_t instances = 0;

template <class T>
T checked(::dlt698::Result<T> result) {
    if (!result) {
        const auto& error = result.error();
        throw std::runtime_error(
            error.context +
            (error.remote_code ? "; remote=" + std::to_string(*error.remote_code) : ""));
    }
    return std::move(result).value();
}

void checked(::dlt698::Result<void> result) {
    if (!result) throw std::runtime_error(result.error().context);
}

std::uint32_t number(std::string text, int base = 10) {
    if (text.size() > 2 && text.substr(0, 2) == "0x") {
        text.erase(0, 2);
        base = 16;
    } else if (text.size() > 2 && text.substr(0, 2) == "0X") {
        text.erase(0, 2);
        base = 16;
    }
    std::uint32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size())
        throw std::invalid_argument("Invalid unsigned address: " + text);
    return value;
}

int integer(const Json& config, const char* key, int fallback, int low, int high) {
    if (!config.contains(key)) return fallback;
    const auto& value = config.at(key);
    if (!value.is_number_integer() || value.get<double>() < low || value.get<double>() > high)
        throw std::invalid_argument(std::string("Invalid ") + key);
    return value.get<int>();
}

Oad parse_oad(const Json& input) {
    std::uint32_t value;
    if (input.is_string()) {
        auto text = input.get<std::string>();
        const auto first = text.find('/');
        if (first != std::string::npos) {
            const auto second = text.find('/', first + 1);
            if (second == std::string::npos) throw std::invalid_argument("OAD needs OI/attr/index");
            const auto oi = number(text.substr(0, first), 16);
            const auto attr = number(text.substr(first + 1, second - first - 1));
            const auto index = number(text.substr(second + 1));
            if (oi > 65535 || attr > 255 || index > 255)
                throw std::invalid_argument("OAD field out of range");
            value = (oi << 16) | (attr << 8) | index;
        } else {
            value = number(text, 16);
        }
    } else {
        if (!input.is_number_integer() || input.get<double>() < 0 ||
            input.get<double>() > std::numeric_limits<std::uint32_t>::max())
            throw std::invalid_argument("OAD must be uint32 or hexadecimal text");
        value = input.get<std::uint32_t>();
    }
    Oad result{static_cast<std::uint16_t>(value >> 16), static_cast<std::uint8_t>(value >> 8),
               static_cast<std::uint8_t>(value)};
    if (!(result.attribute & 31)) throw std::invalid_argument("OAD attribute must be nonzero");
    return result;
}

std::uint32_t key(Oad oad) {
    return (std::uint32_t(oad.oi) << 16) | (std::uint32_t(oad.attribute) << 8) | oad.index;
}

template <class T>
Data numeric(double value) {
    using Value = decltype(T{}.value);
    if (!std::isfinite(value)) throw std::invalid_argument("Non-finite numeric value");
    if constexpr (std::is_same_v<Value, bool>) {
        if (value != 0 && value != 1) throw std::invalid_argument("Boolean requires 0 or 1");
    } else if constexpr (std::is_integral_v<Value>) {
        // double 接口不能保证大于 2^53 的整数逐位精确，不接受有损写入。
        if (std::trunc(value) != value || std::abs(value) > 9007199254740991.0 ||
            static_cast<long double>(value) < std::numeric_limits<Value>::lowest() ||
            static_cast<long double>(value) > std::numeric_limits<Value>::max())
            throw std::invalid_argument("Integer value exceeds range or precision");
    } else if (std::abs(value) > std::numeric_limits<Value>::max()) {
        throw std::invalid_argument("Floating value exceeds range");
    }
    return T{static_cast<Value>(value)};
}

Data numeric(const std::string& type, double value) {
    if (type == "boolean" || type == "bool") return numeric<model::Boolean>(value);
    if (type == "int8") return numeric<model::Int8>(value);
    if (type == "int16") return numeric<model::Int16>(value);
    if (type == "int32") return numeric<model::Int32>(value);
    if (type == "int64") return numeric<model::Int64>(value);
    if (type == "uint8") return numeric<model::UInt8>(value);
    if (type == "uint16") return numeric<model::UInt16>(value);
    if (type == "uint32") return numeric<model::UInt32>(value);
    if (type == "uint64") return numeric<model::UInt64>(value);
    if (type == "enum") return numeric<model::Enum>(value);
    if (type == "float32") return numeric<model::Float32>(value);
    if (type == "float64") return numeric<model::Float64>(value);
    throw std::invalid_argument("Unsupported scalar type: " + type);
}

::dlt698::Bytes unhex(const std::string& text) {
    if (text.empty() || text.size() % 2 || text.size() > 131072)
        throw std::invalid_argument("Invalid data_hex length");
    ::dlt698::Bytes bytes;
    for (std::size_t i = 0; i < text.size(); i += 2)
        bytes.push_back(static_cast<std::uint8_t>(number(text.substr(i, 2), 16)));
    return bytes;
}

Data configured_data(const Json& point) {
    if (point.contains("data_hex")) {
        const auto bytes = unhex(point.at("data_hex").get<std::string>());
        return checked(::dlt698::codec::decode_data(bytes));
    }
    const auto value = point.at("value");
    return numeric(point.at("type").get<std::string>(),
                   value.is_boolean() ? (value.get<bool>() ? 1 : 0) : value.get<double>());
}

double scalar(const Data& data) {
    return std::visit(
        [](const auto& item) -> double {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, model::Boolean> || std::is_same_v<T, model::Int8> ||
                          std::is_same_v<T, model::Int16> || std::is_same_v<T, model::Int32> ||
                          std::is_same_v<T, model::Int64> || std::is_same_v<T, model::UInt8> ||
                          std::is_same_v<T, model::UInt16> || std::is_same_v<T, model::UInt32> ||
                          std::is_same_v<T, model::UInt64> || std::is_same_v<T, model::Enum> ||
                          std::is_same_v<T, model::Float32> || std::is_same_v<T, model::Float64>) {
                const auto value = static_cast<double>(item.value);
                if (!std::isfinite(value)) throw std::invalid_argument("Non-finite sample");
                if constexpr (std::is_same_v<T, model::Int64> || std::is_same_v<T, model::UInt64>) {
                    if (std::abs(static_cast<long double>(item.value)) > 9007199254740991.0L)
                        throw std::invalid_argument("Sample exceeds exact double integer range");
                }
                return value;
            }
            throw std::invalid_argument("Data is not scalar; select an OAD element index");
        },
        data.payload);
}

void sample(Json& result, const ObjectValue& value) {
    if (const auto dar = std::get_if<std::uint8_t>(&value)) {
        result["dar"] = *dar;
        result["error"] = "DLT698 access result DAR=" + std::to_string(*dar);
        return;
    }
    const auto& data = std::get<Data>(value);
    result["data_type"] = static_cast<int>(data.type());
    const auto bytes = checked(::dlt698::codec::encode_data(data));
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string hex;
    for (auto byte : bytes) {
        hex += digits[byte >> 4];
        hex += digits[byte & 15];
    }
    result["data_hex"] = std::move(hex);
    result["value"] = scalar(data);
    result["quality"] = enum_value(SampleQuality::Good);
}
}  // namespace

class Dlt698Protocol::Impl {
   public:
    Json config;
    std::string error;
    bool configured{false};
    bool server_role{false};
    bool tcp{true};
    ::dlt698::session::SessionOptions options;
    ::dlt698::transport::SerialOptions serial;
    app::ConnectionProfile profile{app::ConnectionProfile::remote_public};
    std::map<std::string, Oad> aliases;
    std::map<std::uint32_t, std::string> write_types;
    std::shared_ptr<::dlt698::service::Device> device;
    std::unique_ptr<app::Client> client;
    std::unique_ptr<app::Server> server;

    Oad point(const std::string& code) const {
        const auto it = aliases.find(code);
        return it == aliases.end() ? parse_oad(code) : it->second;
    }
};

Dlt698Protocol::Dlt698Protocol() : impl_(std::make_unique<Impl>()) {
    std::lock_guard<std::mutex> lock(lifetime_mutex);
    ++instances;
}

Dlt698Protocol::~Dlt698Protocol() {
    disconnect();
    impl_.reset();
    std::lock_guard<std::mutex> lock(lifetime_mutex);
    if (--instances == 0) ::dlt698::app::detail::JoinQueue::shutdown_idle();
}

int Dlt698Protocol::initialize(const std::string& text) {
    try {
        if (is_connected()) throw std::invalid_argument("Disconnect before reinitializing");
        auto next = std::make_unique<Impl>();
        auto config = Json::parse(text);
        if (!config.is_object()) throw std::invalid_argument("Config must be object");
        if (config.contains("settings")) {
            auto settings = config.at("settings");
            if (settings.is_string())
                settings = settings.get<std::string>().empty()
                               ? Json::object()
                               : Json::parse(settings.get<std::string>());
            if (!settings.is_object()) throw std::invalid_argument("settings must be object");
            for (auto it = settings.begin(); it != settings.end(); ++it)
                if (!config.contains(it.key())) config[it.key()] = it.value();
        }
        if (config.value("protocol", std::string("DLT698")) != "DLT698")
            throw std::invalid_argument("Protocol must be DLT698");
        const auto role = config.value("local_role", config.value("role", std::string("client")));
        if (role != "client" && role != "server" && role != "server_readonly")
            throw std::invalid_argument("Invalid role");
        next->server_role = role != "client";
        const auto connection = config.value("connection", std::string());
        const auto transport = config.value(
            "transport", config.contains("serial_port") ||
                                 (connection.find(':') == std::string::npos && !connection.empty())
                             ? std::string("rtu")
                             : std::string("tcp"));
        if (transport != "tcp" && transport != "rtu" && transport != "serial")
            throw std::invalid_argument("Invalid transport");
        next->tcp = transport == "tcp";
        const auto profile =
            config.value("connection_profile", next->tcp ? "remote_public" : "local_public");
        if (profile == "remote_public")
            next->profile = app::ConnectionProfile::remote_public;
        else if (profile == "local_public")
            next->profile = app::ConnectionProfile::local_public;
        else
            throw std::invalid_argument("Invalid connection_profile");

        auto meter =
            config.value("meter_address", config.value("rtu_addr", std::string("000000000000")));
        if (meter.empty() || meter.size() > 32 ||
            meter.find_first_not_of("0123456789") != std::string::npos)
            throw std::invalid_argument("Meter address needs 1..32 decimal digits");
        if (meter.size() <= 12) meter.insert(0, 12 - meter.size(), '0');
        if (meter.size() % 2) throw std::invalid_argument("Variable address needs even digits");
        next->options.server.bytes = unhex(meter);
        std::reverse(next->options.server.bytes.begin(), next->options.server.bytes.end());
        next->options.server.logical = integer(config, "logical_address", 0, 0, 3);
        next->options.client_address = integer(config, "client_address", 0, 0, 255);
        // 与现有通道一致：旧 timeout 使用秒，显式 timeout_ms 优先。
        const int timeout = config.contains("timeout_ms")
                                ? integer(config, "timeout_ms", 5000, 1, 600000)
                                : integer(config, "timeout", 5, 1, 600) * 1000;
        next->options.request_timeout = std::chrono::milliseconds(timeout);
        checked(::dlt698::session::validate_options(next->options));
        if (next->tcp) {
            auto host = config.value(
                "ip", config.value("host", next->server_role ? std::string("0.0.0.0")
                                                             : std::string("127.0.0.1")));
            int port = 6980;
            if (!connection.empty()) {
                const auto colon = connection.rfind(':');
                if (colon == std::string::npos) throw std::invalid_argument("Expected host:port");
                host = connection.substr(0, colon);
                const auto parsed = number(connection.substr(colon + 1));
                if (parsed > 65535) throw std::invalid_argument("Invalid port");
                port = static_cast<int>(parsed);
            }
            if (host.size() > 2 && host.front() == '[' && host.back() == ']')
                host = host.substr(1, host.size() - 2);
            if (host.empty()) throw std::invalid_argument("Empty host");
            config["ip"] = host;
            config["port"] = integer(config, "port", port, 1, 65535);
            if (config.at("port") == 0) throw std::invalid_argument("Invalid port");
        } else {
            const auto path = config.value("serial_port", connection);
            if (path.empty()) throw std::invalid_argument("Empty serial_port");
            config["serial_port"] = path;
            next->serial.baud_rate = integer(
                config, "baud_rate", integer(config, "baudrate", 9600, 1, 4000000), 1, 4000000);
            next->serial.data_bits =
                integer(config, "data_bits", integer(config, "databits", 8, 5, 8), 5, 8);
            const auto stop =
                integer(config, "stop_bits", integer(config, "stopbits", 1, 1, 2), 1, 2);
            next->serial.stop_bits = stop == 1 ? ::dlt698::transport::SerialStopBits::one
                                               : ::dlt698::transport::SerialStopBits::two;
            const auto parity = config.value("parity", std::string("even"));
            if (parity == "even" || parity == "E" || parity == "e")
                next->serial.parity = ::dlt698::transport::SerialParity::even;
            else if (parity == "odd" || parity == "O" || parity == "o")
                next->serial.parity = ::dlt698::transport::SerialParity::odd;
            else if (parity == "none" || parity == "N" || parity == "n")
                next->serial.parity = ::dlt698::transport::SerialParity::none;
            else
                throw std::invalid_argument("Invalid parity");
        }
        if (config.contains("allow_writes") && !config.at("allow_writes").is_boolean())
            throw std::invalid_argument("allow_writes must be boolean");
        config["max_connections"] = integer(config, "max_connections", 16, 1, 1024);
        config["heartbeat_seconds"] = integer(config, "heartbeat_seconds", 5, 0, 65535);
        const auto points = config.value("points", Json::array());
        if (!points.is_array() || points.size() > enum_value(PluginLimits::MaxBatchPoints))
            throw std::invalid_argument("Invalid points array");
        next->device = std::make_shared<::dlt698::service::Device>();
        std::map<std::uint16_t, ::dlt698::service::ObjectSchema> custom;
        std::set<std::uint32_t> published;
        for (const auto& entry : points) {
            const auto oad = parse_oad(entry.at("oad"));
            if (entry.contains("code")) {
                const auto code = entry.at("code").get<std::string>();
                if (code.empty() || !next->aliases.emplace(code, oad).second)
                    throw std::invalid_argument("Empty or duplicate alias");
            }
            if (entry.contains("type")) {
                const auto type = entry.at("type").get<std::string>();
                (void)numeric(type, 0);
                const auto inserted = next->write_types.emplace(key(oad), type);
                if (!inserted.second && inserted.first->second != type)
                    throw std::invalid_argument("Conflicting OAD types");
            }
            if (!next->server_role || (!entry.contains("value") && !entry.contains("data_hex")))
                continue;
            if (oad.index || oad.attribute > 31 || !published.insert(key(oad)).second)
                throw std::invalid_argument("Publish each full attribute exactly once");
            const auto data = configured_data(entry);
            if (!::dlt698::standard::find_object(oad.oi)) {
                auto& schema = custom[oad.oi];
                schema.oi = oad.oi;
                schema.name = "EMS";
                schema.attributes.push_back({oad.attribute, data.type(), true, false, false});
            }
        }
        for (auto& [oi, schema] : custom) checked(next->device->define(std::move(schema)));
        if (next->server_role)
            for (const auto& entry : points)
                if (entry.contains("value") || entry.contains("data_hex"))
                    checked(next->device->set(parse_oad(entry.at("oad")), configured_data(entry)));
        next->config = std::move(config);
        next->configured = true;
        if (disconnect() < 0) return -1;
        impl_ = std::move(next);
        return 0;
    } catch (const std::exception& error) {
        impl_->error = error.what();
        return -1;
    }
}

int Dlt698Protocol::connect() {
    try {
        if (is_connected()) return 0;
        if (!impl_->configured) throw std::invalid_argument("Initialize before connecting");
        if (disconnect() < 0) return -1;
        const auto& cfg = impl_->config;
        if (impl_->server_role) {
            app::ServerOptions options;
            options.protocol = impl_->options;
            options.max_connections = cfg.at("max_connections").get<std::size_t>();
            options.heartbeat_seconds = cfg.at("heartbeat_seconds").get<std::uint16_t>();
            impl_->server = std::make_unique<app::Server>(impl_->device, options);
            if (impl_->tcp)
                checked(impl_->server->start_tcp(cfg.at("ip").get<std::string>(),
                                                 cfg.at("port").get<std::uint16_t>(),
                                                 impl_->profile));
            else
                checked(impl_->server->start_serial(cfg.at("serial_port").get<std::string>(),
                                                    impl_->serial, {}, impl_->profile));
        } else {
            app::ClientOptions options;
            options.protocol = impl_->options;
            options.transport_timeout = options.login_timeout = impl_->options.request_timeout;
            impl_->client = std::make_unique<app::Client>(options);
            if (impl_->tcp)
                checked(impl_->client->connect_tcp(cfg.at("ip").get<std::string>(),
                                                   cfg.at("port").get<std::uint16_t>(),
                                                   impl_->profile));
            else
                checked(impl_->client->open_serial(cfg.at("serial_port").get<std::string>(),
                                                   impl_->serial, {}, impl_->profile));
        }
        impl_->error.clear();
        return 0;
    } catch (const std::exception& error) {
        const std::string message = error.what();
        disconnect();
        impl_->error = message;
        return -1;
    }
}

int Dlt698Protocol::disconnect() {
    try {
        if (impl_->client) {
            // 上游保证即使 RELEASE 失败也完成资源收尾。
            const auto result = impl_->client->disconnect();
            impl_->client.reset();
            checked(result);
        }
        if (impl_->server) {
            checked(impl_->server->stop());
            impl_->server.reset();
        }
        impl_->error.clear();
        return 0;
    } catch (const std::exception& error) {
        impl_->error = error.what();
        return -1;
    }
}

bool Dlt698Protocol::is_connected() const {
    return impl_->server_role
               ? impl_->server && impl_->server->state() == app::ServerState::running
               : impl_->client && impl_->client->state() == app::ClientState::connected;
}

int Dlt698Protocol::read_batch(const std::vector<std::string>& codes, std::string& output) {
    try {
        if (!is_connected()) throw std::runtime_error("DLT698 is not connected");
        if (codes.size() > enum_value(PluginLimits::MaxBatchPoints))
            throw std::invalid_argument("Batch too large");
        Json results = Json::array();
        int valid = 0;
        // 小块 GET NormalList，隔离坏地址并限制单次请求占用。
        for (std::size_t begin = 0; begin < codes.size(); begin += 32) {
            std::vector<Oad> attributes;
            std::vector<std::size_t> positions;
            const auto end = std::min(begin + 32, codes.size());
            for (auto i = begin; i < end; ++i) {
                const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::system_clock::now().time_since_epoch())
                                           .count();
                results.push_back({{"code", codes[i]},
                                   {"value", nullptr},
                                   {"quality", enum_value(SampleQuality::Invalid)},
                                   {"timestamp", timestamp}});
                try {
                    const auto oad = impl_->point(codes[i]);
                    results[i]["oad"] = key(oad);
                    attributes.push_back(oad);
                    positions.push_back(i);
                } catch (const std::exception& error) {
                    results[i]["error"] = error.what();
                }
            }
            std::vector<ObjectValue> values;
            try {
                if (attributes.empty()) continue;
                if (impl_->server_role) {
                    for (auto oad : attributes) values.push_back(impl_->device->get(oad));
                } else {
                    const auto response = checked(impl_->client->get_list(attributes));
                    if (response.attributes.size() != attributes.size())
                        throw std::runtime_error("GET response size mismatch");
                    for (std::size_t i = 0; i < attributes.size(); ++i) {
                        if (!(response.attributes[i].attribute == attributes[i]))
                            throw std::runtime_error("GET response OAD mismatch");
                        values.push_back(response.attributes[i].result);
                    }
                }
            } catch (const std::exception& error) {
                for (auto i : positions) results[i]["error"] = error.what();
                continue;
            }
            for (std::size_t i = 0; i < values.size(); ++i) {
                auto& result = results[positions[i]];
                try {
                    sample(result, values[i]);
                } catch (const std::exception& error) {
                    result["error"] = error.what();
                }
                if (result.at("quality") == enum_value(SampleQuality::Good)) ++valid;
            }
        }
        output = results.dump();
        impl_->error.clear();
        return valid;
    } catch (const std::exception& error) {
        impl_->error = error.what();
        return -1;
    }
}

int Dlt698Protocol::write(const std::string& code, double value) {
    try {
        if (!is_connected()) throw std::runtime_error("DLT698 is not connected");
        const auto oad = impl_->point(code);
        const auto type = impl_->write_types.find(key(oad));
        if (type == impl_->write_types.end())
            throw std::invalid_argument("Configure scalar type before writing");
        auto data = numeric(type->second, value);
        if (impl_->server_role) {
            if (oad.index)
                checked(impl_->device->set_element(oad, std::move(data)));
            else
                checked(impl_->device->set(oad, std::move(data)));
        } else {
            if (!impl_->config.value("allow_writes", false))
                throw std::invalid_argument("Client SET requires allow_writes");
            const auto dar = checked(impl_->client->set(oad, std::move(data)));
            if (dar) throw std::runtime_error("DLT698 SET DAR=" + std::to_string(dar));
        }
        impl_->error.clear();
        return 0;
    } catch (const std::exception& error) {
        impl_->error = error.what();
        return -1;
    }
}

std::string Dlt698Protocol::last_error() const { return impl_->error; }
}  // namespace ems::net::protocol
