#include "net/protocol/dlt645_protocol.h"

#include <dlt645/service/client_service.h>
#include <dlt645/service/server_service.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ems::net::protocol {
namespace {
using Json = nlohmann::json;
using Item = dlt645::model::DataItem;
using Meter = dlt645::service::ServerService;
using Frame = dlt645::protocol::Frame;
namespace client = dlt645::transport::client;
namespace server = dlt645::transport::server;
using Clock = std::chrono::system_clock;

// 上游单例用裸 new 保存定义；插件承担其生命周期，在库卸载时释放只读注册表。
dlt645::model::DataItemManager& definitions() {
    static const std::unique_ptr<dlt645::model::DataItemManager> registry(DIManager::inst());
    return *registry;
}

std::int64_t millis(Clock::time_point time = Clock::now()) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}

std::uint32_t number(const std::string& text) {
    std::uint32_t value = 0;
    const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    const auto begin = text.data() + (hex ? 2 : 0);
    const auto parsed = std::from_chars(begin, text.data() + text.size(), value, hex ? 16 : 10);
    if (begin == text.data() + text.size() || parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size())
        throw std::invalid_argument("Invalid unsigned address: " + text);
    return value;
}

std::uint32_t di_value(const Json& value) {
    if (value.is_string()) return number(value.get<std::string>());
    if ((!value.is_number_integer() && !value.is_number_unsigned()) || value.get<double>() < 0 ||
        value.get<double>() > 4294967295.0)
        throw std::invalid_argument("DI must be uint32 or decimal/0x text");
    return value.get<std::uint32_t>();
}

int integer(const Json& config, const char* name, int fallback, int low, int high) {
    if (!config.contains(name)) return fallback;
    const auto& value = config.at(name);
    if (!value.is_number_integer() || value.get<double>() < low || value.get<double>() > high)
        throw std::invalid_argument(std::string("Invalid ") + name);
    return value.get<int>();
}

std::string meter_text(std::string text) {
    if (text.empty() || text.size() > 12 ||
        !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
        throw std::invalid_argument("Meter address must contain 1..12 decimal digits");
    return std::string(12 - text.size(), '0') + text;
}

template <std::size_t N>
std::array<std::uint8_t, N> bcd(const std::string& text, bool little_endian = true) {
    if (text.size() != N * 2 ||
        !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
        throw std::invalid_argument("Invalid packed BCD address/password");
    std::array<std::uint8_t, N> result{};
    for (std::size_t i = 0; i < N; ++i) {
        const auto pos = text.size() - (i + 1) * 2;
        result[i] = static_cast<std::uint8_t>(((text[pos] - '0') << 4) | (text[pos + 1] - '0'));
    }
    if (!little_endian) std::reverse(result.begin(), result.end());
    return result;
}

struct Point {
    std::string meter;
    std::uint32_t di{0};
    int field{-1};
};

Point parse_point(const std::string& code, const std::string& default_meter) {
    Point point{default_meter, 0, -1};
    std::string address = code;
    const auto colon = address.find(':');
    if (colon != std::string::npos && colon == 12) {
        point.meter = meter_text(address.substr(0, colon));
        address.erase(0, colon + 1);
    }
    const auto field = address.find(':');
    if (field != std::string::npos) {
        const auto index = number(address.substr(field + 1));
        if (index > 65535) throw std::invalid_argument("Field index out of range");
        point.field = static_cast<int>(index);
        address.resize(field);
    }
    point.di = number(address);
    if ((point.di >> 24) > 4) throw std::invalid_argument("Only DI classes 00..04 are supported");
    return point;
}

// X / N 为十进制数位；日期、时间及成对事件字段保留原始字符串。
std::string numeric_field(const std::string& format, double value) {
    if (!std::isfinite(value) || value < 0 || format.empty() ||
        format.find_first_not_of("XN.") != std::string::npos ||
        std::count(format.begin(), format.end(), '.') > 1)
        throw std::invalid_argument("Field is not an unsigned numeric parameter");
    const auto dot = format.find('.');
    const auto precision = dot == std::string::npos ? 0u : format.size() - dot - 1;
    const auto width =
        format.size() - static_cast<std::size_t>(std::count(format.begin(), format.end(), '.'));
    const double scaled = value * std::pow(10.0, static_cast<double>(precision));
    if (width == 0 || scaled >= std::pow(10.0, static_cast<double>(width)) ||
        std::abs(scaled - std::round(scaled)) > 1e-6 || width > 15)
        throw std::invalid_argument("Value exceeds parameter range or precision");
    auto result = std::to_string(static_cast<std::uint64_t>(std::llround(scaled)));
    return std::string(width - result.size(), '0') + result;
}

double field_value(const dlt645::model::DataField& field) {
    if (field.dataFormat.empty() ||
        field.dataFormat.find_first_not_of("XN.") != std::string::npos || field.value.empty() ||
        field.value.find(',') != std::string::npos ||
        !std::all_of(field.value.begin(), field.value.end(),
                     [](char c) { return c >= '0' && c <= '9'; }))
        throw std::invalid_argument("Field has no scalar numeric value");
    std::size_t used = 0;
    double value = std::stod(field.value, &used);
    if (used != field.value.size() || field.value.size() > 15)
        throw std::invalid_argument("Field exceeds numeric precision");
    const auto dot = field.dataFormat.find('.');
    if (dot != std::string::npos)
        value /= std::pow(10.0, static_cast<double>(field.dataFormat.size() - dot - 1));
    return value;
}

Json result_item(const std::string& code, const std::shared_ptr<Item>& item, int field) {
    Json result = {{"code", code}, {"value", nullptr}, {"quality", 1}, {"timestamp", millis()}};
    if (!item) return result;
    result["di"] = item->di;
    result["unit"] = item->unit;
    if (millis(item->timestamp) > 0) result["timestamp"] = millis(item->timestamp);
    if (!item->fields.empty()) {
        result["fields"] = Json::array();
        for (const auto& entry : item->fields)
            result["fields"].push_back({{"name", entry.name},
                                        {"format", entry.dataFormat},
                                        {"unit", entry.unit},
                                        {"value", entry.value}});
    }
    try {
        double value;
        if (field >= 0) {
            if (static_cast<std::size_t>(field) >= item->fields.size())
                throw std::invalid_argument("Field index out of bounds");
            value = field_value(item->fields[static_cast<std::size_t>(field)]);
        } else if (const auto demand = std::get_if<dlt645::model::Demand>(&item->value)) {
            value = demand->value;
            result["occur_time"] = millis(demand->occurTime);
        } else if (const auto real = std::get_if<float>(&item->value))
            value = *real;
        else if (const auto signed_value = std::get_if<std::int32_t>(&item->value))
            value = *signed_value;
        else if (const auto unsigned_value = std::get_if<std::uint32_t>(&item->value))
            value = *unsigned_value;
        else if (!item->fields.empty())
            value = field_value(item->fields.front());
        else
            throw std::invalid_argument("Item has no numeric value");
        if (!std::isfinite(value)) throw std::invalid_argument("Non-finite value");
        result["value"] = value;
        result["quality"] = 0;
    } catch (const std::exception& error) {
        result["error"] = error.what();
    }
    return result;
}

struct MeterImage {
    std::shared_ptr<Meter> service;
    std::set<std::uint32_t> exposed;
};

struct ReadOnlyHandler final : server::ConnectionHandler {
    std::mutex mutex;
    std::map<std::string, MeterImage> meters;
    bool little_endian{true};

    std::vector<std::uint8_t> handleRequest(const Frame& frame) override {
        std::lock_guard<std::mutex> lock(mutex);
        const bool discovery =
            std::all_of(frame.addr.begin(), frame.addr.end(), [](auto b) { return b == 0xAA; });
        for (auto& [address, image] : meters) {
            if (frame.addr != bcd<6>(address, little_endian) && !(discovery && meters.size() == 1))
                continue;
            // 上送出口拒绝全部远端写和管理命令，广播请求不应答也不改变映像。
            if (frame.ctrlCode != 0x11 && frame.ctrlCode != 0x13)
                return Frame::buildFrame(frame.addr,
                                         static_cast<std::uint8_t>(frame.ctrlCode | 0xC0), {0x04});
            if (frame.ctrlCode == 0x11) {
                if (frame.data.size() != 4) return Frame::buildFrame(frame.addr, 0xD1, {0x02});
                std::uint32_t di = 0;
                for (unsigned i = 0; i < 4; ++i) di |= std::uint32_t(frame.data[i]) << (i * 8);
                if (!image.exposed.count(di)) return Frame::buildFrame(frame.addr, 0xD1, {0x02});
            }
            return image.service->handleRequest(frame);
        }
        return {};
    }

    void onConnectionClosed() override {}
};

void set_numeric(MeterImage& image, std::uint32_t di, double value) {
    if (!std::isfinite(value)) throw std::invalid_argument("Non-finite value");
    const auto category = di >> 24;
    const auto item = image.service->getDataItem(di);
    if (!item) throw std::invalid_argument("Unknown standard DI");
    bool success = false;
    if (category <= 2) {
        // 先验证调用方的十进制精度，再交给协议栈处理有符号范围与 BCD 编码。
        (void)numeric_field(item->dataFormat, std::abs(value));
        const auto raw = static_cast<float>(value);
        if (!std::isfinite(raw)) throw std::invalid_argument("Value exceeds float range");
        // 需量发生时间在本地更新时记录；配置可单独覆盖。
        if (category == 0) success = image.service->set00(di, raw);
        if (category == 1)
            success = image.service->set01(di, dlt645::model::Demand(raw, Clock::now()));
        if (category == 2) success = image.service->set02(di, raw);
    } else if (item->fields.size() == 1) {
        const std::vector<std::string> values{
            numeric_field(item->fields.front().dataFormat, value)};
        success =
            category == 3 ? image.service->set03(di, values) : image.service->set04(di, values);
    }
    if (!success)
        throw std::invalid_argument("Cannot represent DI value; use fields for compound items");
    image.exposed.insert(di);
}
}  // namespace

class Dlt645Protocol::Impl {
   public:
    Json config;
    std::string error;
    std::string meter{"000000000000"};
    std::map<std::string, Point> aliases;
    bool configured{false};
    bool server_role{false};
    bool tcp{true};
    bool little_endian{true};
    std::shared_ptr<client::Connection> connection;
    std::shared_ptr<dlt645::service::ClientService> service;
    std::shared_ptr<server::Server> listener;
    std::shared_ptr<ReadOnlyHandler> image;

    Point point(const std::string& code) const {
        const auto found = aliases.find(code);
        return found == aliases.end() ? parse_point(code, meter) : found->second;
    }

    void build_image() {
        image = std::make_shared<ReadOnlyHandler>();
        image->little_endian = little_endian;
        auto add_meter = [&](const std::string& address) {
            if (image->meters.count(address)) return;
            auto service = std::make_shared<Meter>(nullptr, bcd<6>(address, little_endian));
            image->meters.emplace(address, MeterImage{std::move(service), {}});
        };
        add_meter(meter);
        for (const auto& entry : config.value("points", Json::array())) {
            const auto address = meter_text(entry.value("meter_address", meter));
            add_meter(address);
            auto& target = image->meters.at(address);
            const auto di = di_value(entry.at("di"));
            if ((di >> 24) > 4) throw std::invalid_argument("Unsupported DI class");
            if (entry.contains("fields")) {
                const auto values = entry.at("fields").get<std::vector<std::string>>();
                const bool ok = (di >> 24) == 3
                                    ? target.service->set03(di, values)
                                    : (di >> 24) == 4 && target.service->set04(di, values);
                if (!ok) throw std::invalid_argument("Invalid compound DI fields");
                target.exposed.insert(di);
            } else {
                set_numeric(target, di, entry.at("value").get<double>());
                if ((di >> 24) == 1 && entry.contains("occur_time")) {
                    if (!entry.at("occur_time").is_number_integer())
                        throw std::invalid_argument("Invalid occur_time");
                    const auto time = Clock::time_point(
                        std::chrono::milliseconds(entry.at("occur_time").get<std::int64_t>()));
                    if (!target.service->set01(
                            di, dlt645::model::Demand(
                                    static_cast<float>(entry.at("value").get<double>()), time)))
                        throw std::invalid_argument("Invalid demand");
                }
            }
        }
    }
};

Dlt645Protocol::Dlt645Protocol() : impl_(std::make_unique<Impl>()) {}

Dlt645Protocol::~Dlt645Protocol() { disconnect(); }

int Dlt645Protocol::initialize(const std::string& text) {
    try {
        if (is_connected()) throw std::invalid_argument("Cannot initialize an active instance");
        auto next = std::make_unique<Impl>();
        auto config = Json::parse(text);
        if (!config.is_object()) throw std::invalid_argument("Config must be an object");
        if (config.contains("settings")) {
            auto settings = config.at("settings");
            if (settings.is_string())
                settings = settings.get<std::string>().empty()
                               ? Json::object()
                               : Json::parse(settings.get<std::string>());
            if (!settings.is_object())
                throw std::invalid_argument("settings must be object or JSON object text");
            for (auto it = settings.begin(); it != settings.end(); ++it)
                if (!config.contains(it.key())) config[it.key()] = it.value();
        }
        next->meter = meter_text(
            config.value("meter_address", config.value("rtu_addr", std::string("000000000000"))));
        const auto password = config.value("password", std::string("00000000"));
        (void)bcd<4>(password);
        // ems_simulate 的 Python 库直接使用大端 BCD；标准表地址默认低字节在前。
        const auto endian = config.value("bcd_endian", std::string("little"));
        if (endian != "little" && endian != "big")
            throw std::invalid_argument("bcd_endian must be little or big");
        next->little_endian = endian == "little";
        auto role = config.value("role", std::string("client"));
        if (role != "client" && role != "server" && role != "server_readonly")
            throw std::invalid_argument("Invalid role");
        next->server_role = role != "client";
        std::string transport = config.value("transport", config.value("mode", std::string()));
        std::transform(transport.begin(), transport.end(), transport.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (transport.empty()) transport = config.contains("serial_port") ? "rtu" : "tcp";
        if (config.contains("connection") && !config.contains("ip") &&
            !config.contains("serial_port")) {
            auto endpoint = config.at("connection").get<std::string>();
            const auto colon = endpoint.rfind(':');
            if (colon != std::string::npos && endpoint.rfind("COM", 0) != 0 &&
                endpoint.rfind("/dev/", 0) != 0) {
                config["ip"] = endpoint.substr(0, colon);
                config["port"] = number(endpoint.substr(colon + 1));
                transport = "tcp";
            } else {
                config["serial_port"] = endpoint;
                transport = "rtu";
            }
        }
        if (transport != "tcp" && transport != "rtu")
            throw std::invalid_argument("transport must be tcp or rtu");
        next->tcp = transport == "tcp";
        // 与现有通道及模拟器一致：旧 timeout 为秒，显式 timeout_ms 优先。
        config["timeout_ms"] = config.contains("timeout_ms")
                                   ? integer(config, "timeout_ms", 3000, 1, 60000)
                                   : integer(config, "timeout", 3, 1, 60) * 1000;
        if (next->tcp) {
            const auto ip =
                config.value("ip", std::string(next->server_role ? "0.0.0.0" : "127.0.0.1"));
            (void)boost::asio::ip::make_address(ip);
            config["ip"] = ip;
            config["port"] = integer(config, "port", 8899, 1, 65535);
        } else {
            const auto port = config.value("serial_port", std::string());
            if (port.empty()) throw std::invalid_argument("serial_port is required");
            config["baud_rate"] = integer(
                config, "baud_rate", integer(config, "baudrate", 9600, 300, 115200), 300, 115200);
            config["data_bits"] =
                integer(config, "data_bits", integer(config, "databits", 8, 5, 8), 5, 8);
            config["stop_bits"] =
                integer(config, "stop_bits", integer(config, "stopbits", 1, 1, 2), 1, 2);
            auto parity = config.value("parity", std::string("even"));
            if (parity == "E" || parity == "e") parity = "even";
            if (parity == "O" || parity == "o") parity = "odd";
            if (parity == "N" || parity == "n") parity = "none";
            if (parity != "even" && parity != "odd" && parity != "none")
                throw std::invalid_argument("Invalid parity");
            config["parity"] = parity;
        }
        if (config.contains("allow_writes") && !config.at("allow_writes").is_boolean())
            throw std::invalid_argument("allow_writes must be boolean");
        const auto points = config.value("points", Json::array());
        if (!points.is_array() || points.size() > 65536)
            throw std::invalid_argument("Invalid points array");
        for (const auto& entry : points) {
            if (!entry.is_object()) throw std::invalid_argument("Point must be object");
            const auto address = meter_text(entry.value("meter_address", next->meter));
            const auto di = di_value(entry.at("di"));
            if ((di >> 24) > 4) throw std::invalid_argument("Unsupported DI class");
            if (entry.contains("code")) {
                const auto code = entry.at("code").get<std::string>();
                if (code.empty() ||
                    !next->aliases
                         .emplace(code, Point{address, di, integer(entry, "field", -1, 0, 65535)})
                         .second)
                    throw std::invalid_argument("Empty or duplicate point alias");
            }
        }
        next->config = std::move(config);
        next->configured = true;
        if (next->server_role) next->build_image();
        if (disconnect() != 0) return -1;
        impl_ = std::move(next);
        return 0;
    } catch (const std::exception& error) {
        impl_->error = error.what();
        return -1;
    }
}

int Dlt645Protocol::connect() {
    try {
        if (is_connected()) return 0;
        if (!impl_->configured) throw std::invalid_argument("Instance has not been initialized");
        if (disconnect() != 0) return -1;
        auto& cfg = impl_->config;
        const auto timeout = std::chrono::milliseconds(cfg.at("timeout_ms").get<int>());
        if (impl_->server_role) {
            if (impl_->tcp) {
                auto transport = std::make_shared<server::TcpServer>();
                server::TcpServerConfig config;
                config.ip = cfg.at("ip");
                config.port = cfg.at("port");
                config.timeout = timeout;
                if (!transport->configure(config))
                    throw std::runtime_error("Cannot configure TCP server");
                impl_->listener = transport;
            } else {
                auto transport = std::make_shared<server::RtuServer>();
                server::RtuServerConfig config;
                config.port = cfg.at("serial_port");
                config.baudRate = cfg.at("baud_rate");
                config.dataBits = cfg.at("data_bits");
                config.stopBits = cfg.at("stop_bits");
                config.parity = cfg.at("parity");
                config.timeout = timeout;
                if (!transport->configure(config))
                    throw std::runtime_error("Cannot configure RTU server");
                impl_->listener = transport;
            }
            impl_->listener->setConnectionHandler(impl_->image);
            if (!impl_->listener->start()) throw std::runtime_error("Cannot start DLT645 server");
        } else {
            if (impl_->tcp) {
                auto transport = std::make_shared<client::TcpClient>();
                client::TcpClientConfig config;
                config.ip = cfg.at("ip");
                config.port = cfg.at("port");
                config.timeout = timeout;
                if (!transport->configure(config))
                    throw std::runtime_error("Cannot configure TCP client");
                impl_->connection = transport;
            } else {
                auto transport = std::make_shared<client::RtuClient>();
                client::RtuClientConfig config;
                config.port = cfg.at("serial_port");
                config.baudRate = cfg.at("baud_rate");
                config.dataBits = cfg.at("data_bits");
                config.stopBits = cfg.at("stop_bits");
                config.parity = cfg.at("parity");
                config.timeout = timeout;
                if (!transport->configure(config))
                    throw std::runtime_error("Cannot configure RTU client");
                impl_->connection = transport;
            }
            // 在首次使用上游单例前注册卸载清理，所有实例销毁后才卸载库。
            (void)definitions();
            impl_->service = std::make_shared<dlt645::service::ClientService>(impl_->connection);
            impl_->service->setAddress(bcd<6>(impl_->meter, impl_->little_endian));
            impl_->service->setPassword(
                bcd<4>(cfg.value("password", std::string("00000000")), impl_->little_endian));
            if (!impl_->connection->connect())
                throw std::runtime_error("Cannot connect to DLT645 endpoint");
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

int Dlt645Protocol::disconnect() {
    try {
        if (impl_->listener) {
            impl_->listener->stop();
            impl_->listener->setConnectionHandler(nullptr);
            impl_->listener.reset();
        }
        if (impl_->connection && impl_->connection->isConnected()) impl_->connection->disconnect();
        impl_->service.reset();
        impl_->connection.reset();
        impl_->error.clear();
        return 0;
    } catch (const std::exception& error) {
        impl_->error = error.what();
        return -1;
    }
}

bool Dlt645Protocol::is_connected() const {
    return impl_->server_role ? impl_->listener && impl_->listener->isRunning()
                              : impl_->connection && impl_->connection->isConnected();
}

int Dlt645Protocol::read_batch(const std::vector<std::string>& codes, std::string& output) {
    try {
        if (!is_connected()) throw std::runtime_error("DLT645 is not connected");
        if (codes.size() > 65536) throw std::invalid_argument("Batch too large");
        Json results = Json::array();
        int valid = 0;
        for (const auto& code : codes) {
            Json result = {
                {"code", code}, {"value", nullptr}, {"quality", 1}, {"timestamp", millis()}};
            try {
                const auto point = impl_->point(code);
                std::shared_ptr<Item> item;
                if (impl_->server_role) {
                    std::lock_guard<std::mutex> lock(impl_->image->mutex);
                    const auto meter = impl_->image->meters.find(point.meter);
                    if (meter != impl_->image->meters.end() &&
                        meter->second.exposed.count(point.di))
                        item = meter->second.service->getDataItem(point.di);
                } else {
                    impl_->service->setAddress(bcd<6>(point.meter, impl_->little_endian));
                    switch (point.di >> 24) {
                        case 0:
                            item = impl_->service->read00(point.di);
                            break;
                        case 1:
                            item = impl_->service->read01(point.di);
                            break;
                        case 2:
                            item = impl_->service->read02(point.di);
                            break;
                        case 3:
                            item = impl_->service->read03(point.di);
                            break;
                        case 4:
                            item = impl_->service->read04(point.di);
                            break;
                    }
                }
                result = result_item(code, item, point.field);
                if (!item) result["error"] = "DI unavailable, unsupported or peer did not reply";
            } catch (const std::exception& error) {
                result["error"] = error.what();
            }
            if (result.at("quality") == 0) ++valid;
            results.push_back(std::move(result));
        }
        output = results.dump();
        impl_->error.clear();
        return valid;
    } catch (const std::exception& error) {
        impl_->error = error.what();
        return -1;
    }
}

int Dlt645Protocol::write(const std::string& code, double value) {
    try {
        if (!is_connected()) throw std::runtime_error("DLT645 is not connected");
        if (!std::isfinite(value)) throw std::invalid_argument("Non-finite write value");
        const auto point = impl_->point(code);
        if (point.field >= 0)
            throw std::invalid_argument("Partial compound writes are not supported");
        if (impl_->server_role) {
            std::lock_guard<std::mutex> lock(impl_->image->mutex);
            const auto meter = impl_->image->meters.find(point.meter);
            if (meter == impl_->image->meters.end()) throw std::invalid_argument("Unknown meter");
            if (!meter->second.exposed.count(point.di))
                throw std::invalid_argument("DI is not exposed by this server");
            set_numeric(meter->second, point.di, value);
        } else {
            if (!impl_->config.value("allow_writes", false) || (point.di >> 24) != 4)
                throw std::invalid_argument("Client writes require allow_writes and a class-04 DI");
            const auto item = definitions().getDataItem(point.di);
            if (!item || item->fields.size() != 1)
                throw std::invalid_argument("Write requires a scalar parameter DI");
            const auto encoded = numeric_field(item->fields.front().dataFormat, value);
            impl_->service->setAddress(bcd<6>(point.meter, impl_->little_endian));
            if (!impl_->service->write04(point.di, encoded))
                throw std::runtime_error("DLT645 parameter write failed");
        }
        impl_->error.clear();
        return 0;
    } catch (const std::exception& error) {
        impl_->error = error.what();
        return -1;
    }
}

std::string Dlt645Protocol::last_error() const { return impl_->error; }
}  // namespace ems::net::protocol
