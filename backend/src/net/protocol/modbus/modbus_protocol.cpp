#include "net/protocol/modbus_protocol.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#else
#include <sys/select.h>
#include <unistd.h>
#endif

#include <modbus.h>

#if !LIBMODBUS_VERSION_CHECK(3, 1, 6)
#error "EMS requires libmodbus >= 3.1.6"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <tuple>

namespace ems::net::protocol {
namespace {
using Json = nlohmann::json;

struct ContextDeleter {
    void operator()(modbus_t* context) const {
        if (context) {
            if (modbus_get_socket(context) != -1) modbus_close(context);
            modbus_free(context);
        }
    }
};

using Context = std::unique_ptr<modbus_t, ContextDeleter>;
using Mapping = std::unique_ptr<modbus_mapping_t, decltype(&modbus_mapping_free)>;

struct Point {
    int slave{1}, function{3}, address{0}, decode{0}, endian{0}, bit{0}, width{1};
    std::size_t index{0};
};

int integer(const std::string& text, int maximum) {
    if (text.empty() || text.front() == '-' || text.front() == '+')
        throw std::invalid_argument("Invalid unsigned point field");
    std::size_t used = 0;
    const auto value =
        std::stoul(text, &used, (text.rfind("0x", 0) == 0 || text.rfind("0X", 0) == 0) ? 16 : 10);
    if (used != text.size() || value > static_cast<unsigned long>(maximum))
        throw std::invalid_argument("Point field is out of range");
    return static_cast<int>(value);
}

Point parse(const std::string& code, bool write) {
    std::vector<std::string> fields;
    std::istringstream input(code);
    std::string field;
    while (std::getline(input, field, ':')) fields.push_back(field);
    if (fields.size() < 3 || fields.size() > 6 || code.back() == ':')
        throw std::invalid_argument("Expected slave:function:offset[:decode[:endian[:bit]]]");
    Point point;
    point.slave = integer(fields[0], 247);
    if (!point.slave) throw std::invalid_argument("Broadcast point access is not supported");
    point.function = integer(fields[1], 16);
    if (write) {
        if (point.function != 1 && point.function != 2 && point.function != 3 &&
            point.function != 4 && point.function != 5 && point.function != 6 &&
            point.function != 15 && point.function != 16)
            throw std::invalid_argument("Unsupported write function");
    } else if (point.function < 1 || point.function > 4) {
        throw std::invalid_argument("Read function must be 1, 2, 3 or 4");
    }
    point.address = integer(fields[2], 65535);
    if (fields.size() >= 4) {
        static const std::map<std::string, std::pair<int, int>> names = {
            {"UINT16", {0, 0}},    {"INT16", {1, 0}},     {"UINT32", {2, 0}},
            {"INT32", {3, 0}},     {"FLOAT_BE", {4, 0}},  {"FLOAT_LE", {4, 1}},
            {"DOUBLE_BE", {5, 0}}, {"DOUBLE_LE", {5, 1}}, {"UINT8", {6, 0}},
            {"INT8", {7, 0}},      {"BOOL", {8, 0}},      {"BIT", {9, 0}}};
        const auto it = names.find(fields[3]);
        if (it != names.end()) {
            point.decode = it->second.first;
            point.endian = it->second.second;
        } else {
            point.decode = integer(fields[3], 9);
        }
    }
    if (fields.size() >= 5) point.endian = integer(fields[4], 3);
    if (fields.size() == 6) {
        if (point.decode != 9) throw std::invalid_argument("Bit offset requires BIT type");
        point.bit = integer(fields[5], 15);
    }
    point.width = point.decode == 5 ? 4 : (point.decode >= 2 && point.decode <= 4 ? 2 : 1);
    if ((point.function == 1 || point.function == 2 || point.function == 5 ||
         point.function == 15) &&
        point.width != 1)
        throw std::invalid_argument("Coil/discrete point must occupy one bit");
    if (point.address + point.width > 65536)
        throw std::invalid_argument("Point crosses the Modbus address limit");
    return point;
}

Json decode(const Point& point, const std::uint16_t* registers) {
    std::uint64_t bits = 0;
    for (int i = 0; i < point.width; ++i) {
        const int index = (point.endian == 1 || point.endian == 3) ? point.width - i - 1 : i;
        auto word = registers[index];
        if (point.endian >= 2) word = static_cast<std::uint16_t>((word >> 8) | (word << 8));
        bits = (bits << 16) | word;
    }
    switch (point.decode) {
        case 1:
            return static_cast<std::int16_t>(bits);
        case 2:
            return static_cast<std::uint32_t>(bits);
        case 3:
            return static_cast<std::int32_t>(bits);
        case 4: {
            const auto raw = static_cast<std::uint32_t>(bits);
            float value;
            std::memcpy(&value, &raw, sizeof(value));
            if (!std::isfinite(value)) throw std::runtime_error("Non-finite FLOAT sample");
            return value;
        }
        case 5: {
            double value;
            std::memcpy(&value, &bits, sizeof(value));
            if (!std::isfinite(value)) throw std::runtime_error("Non-finite DOUBLE sample");
            return value;
        }
        case 6:
            return static_cast<std::uint8_t>(bits);
        case 7:
            return static_cast<std::int8_t>(bits);
        case 8:
            return bits != 0;
        case 9:
            return ((bits >> point.bit) & 1) != 0;
        default:
            return static_cast<std::uint16_t>(bits);
    }
}

std::array<std::uint16_t, 4> encode(const Point& point, double value) {
    if (!std::isfinite(value)) throw std::invalid_argument("Write value must be finite");
    std::uint64_t bits = 0;
    if (point.decode == 4) {
        const float real = static_cast<float>(value);
        if (!std::isfinite(real)) throw std::invalid_argument("FLOAT write value overflows");
        std::uint32_t raw;
        std::memcpy(&raw, &real, sizeof(raw));
        bits = raw;
    } else if (point.decode == 5) {
        std::memcpy(&bits, &value, sizeof(bits));
    } else {
        static constexpr double minimum[] = {0, -32768, 0, -2147483648.0, 0, 0, 0, -128, 0, 0};
        static constexpr double maximum[] = {
            65535, 32767, 4294967295.0, 2147483647.0, 0, 0, 255, 127, 1, 1};
        if (std::trunc(value) != value || value < minimum[point.decode] ||
            value > maximum[point.decode])
            throw std::invalid_argument("Integer write is out of range");
        bits = static_cast<std::uint64_t>(static_cast<std::int64_t>(value));
    }
    std::array<std::uint16_t, 4> registers{};
    for (int i = 0; i < point.width; ++i) {
        auto word = static_cast<std::uint16_t>(bits >> (16 * (point.width - i - 1)));
        if (point.endian >= 2) word = static_cast<std::uint16_t>((word >> 8) | (word << 8));
        const int index = (point.endian == 1 || point.endian == 3) ? point.width - i - 1 : i;
        registers[index] = word;
    }
    return registers;
}

void close_socket(int socket) {
    if (socket == -1) return;
#ifdef _WIN32
    closesocket(static_cast<SOCKET>(socket));
#else
    ::close(socket);
#endif
}

}  // namespace

class ModbusProtocol::Impl {
   public:
    mutable std::mutex mutex;
    Context context;
    std::map<int, Mapping> mappings;
    std::thread worker;
    std::atomic<bool> running{false};
    bool initialized{false}, server{false}, rtu{false};
    std::string endpoint, error;
    int port{502}, baud{9600}, data_bits{8}, stop_bits{1}, slave{1}, timeout{1000};
    int image_size{1024}, max_sessions{16}, listener{-1};
    char parity{'N'};
    std::vector<int> slaves{1};

    Context make_context() const {
        Context result(rtu ? modbus_new_rtu(endpoint.c_str(), baud, parity, data_bits, stop_bits)
                           : modbus_new_tcp(endpoint.c_str(), port));
        if (!result) throw std::runtime_error(modbus_strerror(errno));
        modbus_set_slave(result.get(), slave);
        modbus_set_response_timeout(result.get(), timeout / 1000, (timeout % 1000) * 1000);
        modbus_set_byte_timeout(result.get(), 0, 100000);
        modbus_set_indication_timeout(result.get(), 0, 100000);
        return result;
    }

    void respond(modbus_t* peer, const std::uint8_t* request, int length) {
        const int header = modbus_get_header_length(peer);
        if (length <= header) return;
        const int function = request[header];
        if (function < 1 || function > 4) {
            // 首期上送服务端只读，外部写不能绕过持久意图与策略仲裁。
            modbus_reply_exception(peer, request, MODBUS_EXCEPTION_ILLEGAL_FUNCTION);
            return;
        }
        const int unit = request[header - 1];
        std::lock_guard<std::mutex> lock(mutex);
        const auto mapping = mappings.find(unit);
        if (mapping == mappings.end()) {
            modbus_reply_exception(peer, request, MODBUS_EXCEPTION_GATEWAY_TARGET);
            return;
        }
        modbus_reply(peer, request, length, mapping->second.get());
    }

    void serve() noexcept {
        try {
            std::vector<Context> peers;
            std::array<std::uint8_t, MODBUS_TCP_MAX_ADU_LENGTH> request{};
            while (running.load()) {
                if (rtu) {
                    const int length = modbus_receive(context.get(), request.data());
                    if (length > 0) respond(context.get(), request.data(), length);
                    continue;
                }
                fd_set readers;
                FD_ZERO(&readers);
                FD_SET(listener, &readers);
                int highest = listener;
                for (const auto& peer : peers) {
                    const int socket = modbus_get_socket(peer.get());
                    FD_SET(socket, &readers);
                    highest = std::max(highest, socket);
                }
                timeval wait{0, 100000};
                if (select(highest + 1, &readers, nullptr, nullptr, &wait) <= 0) continue;
                if (FD_ISSET(listener, &readers)) {
                    const int socket = modbus_tcp_accept(context.get(), &listener);
                    if (socket >= 0) {
                        modbus_set_socket(context.get(), -1);
                        if (peers.size() >= static_cast<std::size_t>(max_sessions)
#ifndef _WIN32
                            || socket >= FD_SETSIZE
#endif
                        ) {
                            close_socket(socket);
                        } else {
                            Context peer;
                            try {
                                peer = make_context();
                            } catch (...) {
                                close_socket(socket);
                                throw;
                            }
                            modbus_set_socket(peer.get(), socket);
                            peers.push_back(std::move(peer));
                        }
                    }
                }
                for (auto it = peers.begin(); it != peers.end();) {
                    const int socket = modbus_get_socket(it->get());
                    if (!FD_ISSET(socket, &readers)) {
                        ++it;
                        continue;
                    }
                    const int length = modbus_receive(it->get(), request.data());
                    if (length > 0) {
                        respond(it->get(), request.data(), length);
                        ++it;
                    } else
                        it = peers.erase(it);
                }
            }
        } catch (const std::exception& exception) {
            std::lock_guard<std::mutex> lock(mutex);
            error = exception.what();
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex);
            error = "Unknown server worker error";
        }
        running.store(false);
    }

    int read(const Point& point, int count, std::uint16_t* registers) {
        if (server) {
            const auto it = mappings.find(point.slave);
            if (it == mappings.end() || point.address + count > image_size) return -1;
            const auto* mapping = it->second.get();
            for (int i = 0; i < count; ++i) {
                switch (point.function) {
                    case 1:
                        registers[i] = mapping->tab_bits[point.address + i];
                        break;
                    case 2:
                        registers[i] = mapping->tab_input_bits[point.address + i];
                        break;
                    case 3:
                        registers[i] = mapping->tab_registers[point.address + i];
                        break;
                    case 4:
                        registers[i] = mapping->tab_input_registers[point.address + i];
                        break;
                }
            }
            return count;
        }
        modbus_set_slave(context.get(), point.slave);
        if (point.function <= 2) {
            std::array<std::uint8_t, 2000> bits{};
            const int result =
                point.function == 1
                    ? modbus_read_bits(context.get(), point.address, count, bits.data())
                    : modbus_read_input_bits(context.get(), point.address, count, bits.data());
            if (result == count)
                for (int i = 0; i < count; ++i) registers[i] = bits[i];
            return result;
        }
        return point.function == 3
                   ? modbus_read_registers(context.get(), point.address, count, registers)
                   : modbus_read_input_registers(context.get(), point.address, count, registers);
    }
};

ModbusProtocol::ModbusProtocol() : impl_(std::make_unique<Impl>()) {}

ModbusProtocol::~ModbusProtocol() { disconnect(); }

int ModbusProtocol::initialize(const std::string& text) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->running || impl_->worker.joinable() || impl_->context) {
        impl_->error = "Disconnect before reconfiguring an instance";
        return -1;
    }
    impl_->initialized = false;
    try {
        auto config = Json::parse(text);
        if (config.contains("settings") && config["settings"].is_string() &&
            !config["settings"].get<std::string>().empty()) {
            const auto settings = Json::parse(config["settings"].get<std::string>());
            if (!settings.is_object())
                throw std::invalid_argument("Settings must be a JSON object");
            for (auto it = settings.begin(); it != settings.end(); ++it)
                if (!config.contains(it.key())) config[it.key()] = it.value();
        }
        if (!config.contains("slave_id") && config.contains("rtu_addr"))
            config["slave_id"] = config["rtu_addr"].is_string()
                                     ? integer(config["rtu_addr"].get<std::string>(), 247)
                                     : config["rtu_addr"].get<int>();
        if (!config.contains("timeout_ms") && config.contains("timeout")) {
            const auto seconds = config["timeout"].get<std::int64_t>();
            if (seconds < 1 || seconds > 60)
                throw std::invalid_argument("Legacy timeout seconds out of range");
            config["timeout_ms"] = seconds * 1000;
        }
        const auto role = config.value("local_role", std::string("client"));
        if (role != "client" && role != "server")
            throw std::invalid_argument("Unsupported Modbus role");
        impl_->server = role == "server";
        const auto transport =
            config.value("transport", config.value("protocol", std::string("tcp")));
        if (transport != "tcp" && transport != "rtu" && transport != "ModbusTCP" &&
            transport != "ModbusRTU")
            throw std::invalid_argument("Unsupported Modbus transport");
        impl_->rtu = transport == "rtu" || transport == "ModbusRTU";
        impl_->port = config.value("port", 502);
        impl_->endpoint = config.value("host", config.value("ip", std::string("127.0.0.1")));
        if (config.contains("connection") && !config.contains("host") && !config.contains("ip")) {
            impl_->endpoint = config.at("connection").get<std::string>();
            if (!impl_->rtu) {
                const auto colon = impl_->endpoint.rfind(':');
                if (colon != std::string::npos) {
                    impl_->port = integer(impl_->endpoint.substr(colon + 1), 65535);
                    impl_->endpoint.resize(colon);
                }
            }
        }
        if (impl_->rtu) impl_->endpoint = config.value("serial_port", impl_->endpoint);
        impl_->baud = impl_->rtu ? config.value("baudrate", config.value("baud_rate", 9600)) : 9600;
        impl_->data_bits = impl_->rtu ? config.value("data_bits", 8) : 8;
        impl_->stop_bits = impl_->rtu ? config.value("stopbits", config.value("stop_bits", 1)) : 1;
        const auto parity =
            impl_->rtu ? config.value("parity", std::string("N")) : std::string("N");
        if (parity.size() != 1 || std::string("NEO").find(parity[0]) == std::string::npos)
            throw std::invalid_argument("Parity must be N, E or O");
        impl_->parity = parity[0];
        impl_->slave = config.value("slave_id", 1);
        impl_->timeout = config.value("timeout_ms", 1000);
        impl_->image_size = config.value("image_size", 1024);
        impl_->max_sessions = config.value("max_sessions", 16);
        impl_->slaves = config.value("slaves", std::vector<int>{impl_->slave});
        if (impl_->endpoint.empty() || impl_->port < 1 || impl_->port > 65535 || impl_->baud <= 0 ||
            impl_->data_bits < 5 || impl_->data_bits > 8 || impl_->stop_bits < 1 ||
            impl_->stop_bits > 2 || impl_->slave < 1 || impl_->slave > 247 || impl_->timeout <= 0 ||
            impl_->timeout > 60000 || impl_->image_size < 1 || impl_->image_size > 65536 ||
            impl_->max_sessions < 1 || impl_->max_sessions > FD_SETSIZE - 1 ||
            impl_->slaves.empty() || impl_->slaves.size() > 247)
            throw std::invalid_argument("Modbus configuration is out of range");
        for (int unit : impl_->slaves)
            if (unit < 1 || unit > 247) throw std::invalid_argument("Slave is out of range");
        if (impl_->rtu && impl_->server &&
            (impl_->slaves.size() != 1 || impl_->slaves[0] != impl_->slave))
            throw std::invalid_argument(
                "RTU server supports one configured slave per serial resource");
        impl_->initialized = true;
        impl_->error.clear();
        return 0;
    } catch (const std::exception& error) {
        impl_->error = error.what();
        return -1;
    }
}

int ModbusProtocol::connect() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->running) return 0;
    if (!impl_->initialized || impl_->worker.joinable()) {
        impl_->error = "Initialize (or disconnect stopped worker) before connecting";
        return -1;
    }
    try {
        impl_->context = impl_->make_context();
        if (impl_->server) {
            impl_->mappings.clear();
            for (int unit : impl_->slaves) {
                Mapping mapping(modbus_mapping_new(impl_->image_size, impl_->image_size,
                                                   impl_->image_size, impl_->image_size),
                                &modbus_mapping_free);
                if (!mapping) throw std::bad_alloc();
                impl_->mappings.emplace(unit, std::move(mapping));
            }
        }
        if (impl_->server && !impl_->rtu) {
            impl_->listener = modbus_tcp_listen(impl_->context.get(), impl_->max_sessions);
            if (impl_->listener < 0) throw std::runtime_error(modbus_strerror(errno));
#ifndef _WIN32
            if (impl_->listener >= FD_SETSIZE)
                throw std::runtime_error("Listener exceeds FD_SETSIZE");
#endif
        } else if (modbus_connect(impl_->context.get()) < 0) {
            throw std::runtime_error(modbus_strerror(errno));
        }
        impl_->running.store(true);
        if (impl_->server) impl_->worker = std::thread([this] { impl_->serve(); });
        impl_->error.clear();
        return 0;
    } catch (const std::exception& error) {
        impl_->error = error.what();
        impl_->running.store(false);
        close_socket(impl_->listener);
        impl_->listener = -1;
        impl_->context.reset();
        impl_->mappings.clear();
        return -1;
    }
}

int ModbusProtocol::disconnect() {
    impl_->running.store(false);
    if (impl_->worker.joinable()) impl_->worker.join();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    close_socket(impl_->listener);
    impl_->listener = -1;
    impl_->context.reset();
    impl_->mappings.clear();
    return 0;
}

bool ModbusProtocol::is_connected() const { return impl_->running.load(); }

int ModbusProtocol::read_batch(const std::vector<std::string>& codes, std::string& output) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    try {
        if (!impl_->running) throw std::runtime_error("Modbus instance is disconnected");
        if (codes.size() > 65536) throw std::invalid_argument("Batch exceeds point budget");
        std::vector<Point> points;
        Json results = Json::array();
        for (std::size_t i = 0; i < codes.size(); ++i) {
            auto point = parse(codes[i], false);
            point.index = i;
            points.push_back(point);
            results.push_back({{"code", codes[i]},
                               {"value", nullptr},
                               {"raw_value", nullptr},
                               {"quality", 3},
                               {"timestamp", 0}});
        }
        std::sort(points.begin(), points.end(), [](const Point& a, const Point& b) {
            return std::tie(a.slave, a.function, a.address) <
                   std::tie(b.slave, b.function, b.address);
        });
        int success = 0;
        impl_->error.clear();
        for (std::size_t first = 0; first < points.size();) {
            const auto& begin = points[first];
            const int limit = begin.function <= 2 ? 2000 : 125;
            int end = begin.address + begin.width;
            std::size_t next = first + 1;
            while (next < points.size()) {
                const auto& point = points[next];
                const int extended = std::max(end, point.address + point.width);
                if (point.slave != begin.slave || point.function != begin.function ||
                    point.address > end || extended - begin.address > limit)
                    break;
                end = extended;
                ++next;
            }
            std::array<std::uint16_t, 2000> registers{};
            const int count = end - begin.address;
            const int received = (!impl_->server && !impl_->running)
                                     ? -1
                                     : impl_->read(begin, count, registers.data());
            const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count();
            for (std::size_t i = first; i < next; ++i) {
                const auto& point = points[i];
                results[point.index]["timestamp"] = timestamp;
                if (received != count) {
                    impl_->error =
                        impl_->server ? "Point outside server image" : modbus_strerror(errno);
                    if (!impl_->server && errno < MODBUS_ENOBASE) impl_->running = false;
                    continue;
                }
                try {
                    const auto* data = registers.data() + point.address - begin.address;
                    const Json value =
                        point.function <= 2 ? Json(data[0] != 0) : decode(point, data);
                    results[point.index]["value"] = value;
                    results[point.index]["raw_value"] = value;
                    results[point.index]["quality"] = 0;
                    ++success;
                } catch (const std::exception& error) {
                    impl_->error = error.what();
                }
            }
            first = next;
        }
        output = results.dump();
        return success;
    } catch (const std::exception& error) {
        impl_->error = error.what();
        output.clear();
        return -1;
    }
}

int ModbusProtocol::write(const std::string& code, double value) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    try {
        if (!impl_->running) throw std::runtime_error("Modbus instance is disconnected");
        auto point = parse(code, true);
        if (!impl_->server && (point.function == 2 || point.function == 4))
            throw std::invalid_argument("Cannot write a read-only client address space");
        if (point.decode == 9)
            throw std::invalid_argument("Register bit writes require a dedicated policy");
        const bool coil = point.function == 1 || point.function == 2 || point.function == 5 ||
                          point.function == 15;
        if (coil && value != 0 && value != 1)
            throw std::invalid_argument("Coil value must be 0 or 1");
        if (!impl_->server && point.function == 6 && point.width != 1)
            throw std::invalid_argument("Function 6 accepts one register");
        const auto data = encode(point, value);
        if (impl_->server) {
            const auto it = impl_->mappings.find(point.slave);
            if (it == impl_->mappings.end() || point.address + point.width > impl_->image_size)
                throw std::invalid_argument("Point outside server image");
            auto* mapping = it->second.get();
            if (coil) {
                auto* bits = point.function == 2 ? mapping->tab_input_bits : mapping->tab_bits;
                bits[point.address] = value != 0;
            } else {
                auto* registers =
                    point.function == 4 ? mapping->tab_input_registers : mapping->tab_registers;
                std::copy_n(data.begin(), point.width, registers + point.address);
            }
        } else {
            modbus_set_slave(impl_->context.get(), point.slave);
            const std::uint8_t bit = value != 0;
            const int result =
                coil ? (point.function == 15
                            ? modbus_write_bits(impl_->context.get(), point.address, 1, &bit)
                            : modbus_write_bit(impl_->context.get(), point.address, bit))
                     : (point.width == 1 && point.function != 16
                            ? modbus_write_register(impl_->context.get(), point.address, data[0])
                            : modbus_write_registers(impl_->context.get(), point.address,
                                                     point.width, data.data()));
            if (result != (coil ? 1 : point.width)) {
                if (errno < MODBUS_ENOBASE) impl_->running = false;
                throw std::runtime_error(modbus_strerror(errno));
            }
        }
        impl_->error.clear();
        return 0;
    } catch (const std::exception& error) {
        impl_->error = error.what();
        return -1;
    }
}

std::string ModbusProtocol::last_error() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->error;
}

}  // namespace ems::net::protocol
