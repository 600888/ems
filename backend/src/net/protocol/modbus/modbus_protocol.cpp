#include "net/protocol/modbus_protocol.h"

#include "net/protocol/encoding_types.h"
#include "net/protocol/modbus/modbus_limits.h"
#include "net/protocol/modbus/modbus_types.h"
#include "net/protocol/protocol_types.h"
#include "net/protocol/serial_types.h"
#include "net/protocol/time_units.h"

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
using modbus::DecodeCode;
using modbus::Defaults;
using modbus::EndianCode;
using modbus::FunctionCode;
using modbus::Layout;
using modbus::Limits;
using modbus::PointField;

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
    int slave{enum_value(Defaults::SlaveId)}, address{0}, bit{0}, width{1};
    FunctionCode function{FunctionCode::ReadHoldingRegisters};
    DecodeCode decode{DecodeCode::UINT16};
    EndianCode endian{EndianCode::BigEndian};
    std::size_t index{0};
};

int integer(const std::string& text, int maximum) {
    if (text.empty() || text.front() == '-' || text.front() == '+')
        throw std::invalid_argument("Invalid unsigned point field");
    std::size_t used = 0;
    const auto value = std::stoul(text, &used,
                                  (text.rfind("0x", 0) == 0 || text.rfind("0X", 0) == 0)
                                      ? enum_value(NumberBase::Hexadecimal)
                                      : enum_value(NumberBase::Decimal));
    if (used != text.size() || value > static_cast<unsigned long>(maximum))
        throw std::invalid_argument("Point field is out of range");
    return static_cast<int>(value);
}

Point parse(const std::string& code, bool write) {
    std::vector<std::string> fields;
    std::istringstream input(code);
    std::string field;
    while (std::getline(input, field, ':')) fields.push_back(field);
    if (fields.size() < enum_value(Limits::MinPointFields) ||
        fields.size() > enum_value(Limits::MaxPointFields) || code.back() == ':')
        throw std::invalid_argument("Expected slave:function:offset[:decode[:endian[:bit]]]");
    Point point;
    point.slave = integer(fields[enum_value(PointField::Slave)], enum_value(Limits::MaxSlaveId));
    if (!point.slave) throw std::invalid_argument("Broadcast point access is not supported");
    point.function =
        static_cast<FunctionCode>(integer(fields[enum_value(PointField::Function)],
                                          enum_value(FunctionCode::WriteMultipleRegisters)));
    if (write) {
        if (point.function != FunctionCode::ReadCoils &&
            point.function != FunctionCode::ReadDiscreteInputs &&
            point.function != FunctionCode::ReadHoldingRegisters &&
            point.function != FunctionCode::ReadInputRegisters &&
            point.function != FunctionCode::WriteSingleCoil &&
            point.function != FunctionCode::WriteSingleRegister &&
            point.function != FunctionCode::WriteMultipleCoils &&
            point.function != FunctionCode::WriteMultipleRegisters)
            throw std::invalid_argument("Unsupported write function");
    } else if (point.function < FunctionCode::ReadCoils ||
               point.function > FunctionCode::ReadInputRegisters) {
        throw std::invalid_argument("Read function must be 1, 2, 3 or 4");
    }
    point.address =
        integer(fields[enum_value(PointField::Address)], enum_value(Limits::MaxAddress));
    if (fields.size() > enum_value(PointField::Decode)) {
        static const std::map<std::string, std::pair<DecodeCode, EndianCode>> names = {
            {"UINT16", {DecodeCode::UINT16, EndianCode::BigEndian}},
            {"INT16", {DecodeCode::INT16, EndianCode::BigEndian}},
            {"UINT32", {DecodeCode::UINT32, EndianCode::BigEndian}},
            {"INT32", {DecodeCode::INT32, EndianCode::BigEndian}},
            {"FLOAT_BE", {DecodeCode::FLOAT, EndianCode::BigEndian}},
            {"FLOAT_LE", {DecodeCode::FLOAT, EndianCode::LittleEndian}},
            {"DOUBLE_BE", {DecodeCode::DOUBLE, EndianCode::BigEndian}},
            {"DOUBLE_LE", {DecodeCode::DOUBLE, EndianCode::LittleEndian}},
            {"UINT8", {DecodeCode::UINT8, EndianCode::BigEndian}},
            {"INT8", {DecodeCode::INT8, EndianCode::BigEndian}},
            {"BOOL", {DecodeCode::BOOL, EndianCode::BigEndian}},
            {"BIT", {DecodeCode::BIT, EndianCode::BigEndian}}};
        const auto it = names.find(fields[enum_value(PointField::Decode)]);
        if (it != names.end()) {
            point.decode = it->second.first;
            point.endian = it->second.second;
        } else {
            point.decode = static_cast<DecodeCode>(
                integer(fields[enum_value(PointField::Decode)], enum_value(DecodeCode::BIT)));
        }
    }
    if (fields.size() > enum_value(PointField::Endian))
        point.endian = static_cast<EndianCode>(integer(
            fields[enum_value(PointField::Endian)], enum_value(EndianCode::LittleEndianByteSwap)));
    if (fields.size() == enum_value(Limits::MaxPointFields)) {
        if (point.decode != DecodeCode::BIT)
            throw std::invalid_argument("Bit offset requires BIT type");
        point.bit = integer(fields[enum_value(PointField::Bit)], enum_value(Limits::MaxBitOffset));
    }
    point.width = point.decode == DecodeCode::DOUBLE
                      ? enum_value(Layout::QuadRegisters)
                      : (point.decode >= DecodeCode::UINT32 && point.decode <= DecodeCode::FLOAT
                             ? enum_value(Layout::DoubleRegisters)
                             : enum_value(Layout::SingleRegisters));
    if ((point.function == FunctionCode::ReadCoils ||
         point.function == FunctionCode::ReadDiscreteInputs ||
         point.function == FunctionCode::WriteSingleCoil ||
         point.function == FunctionCode::WriteMultipleCoils) &&
        point.width != 1)
        throw std::invalid_argument("Coil/discrete point must occupy one bit");
    if (point.address + point.width > enum_value(Limits::AddressCount))
        throw std::invalid_argument("Point crosses the Modbus address limit");
    return point;
}

Json decode(const Point& point, const std::uint16_t* registers) {
    std::uint64_t bits = 0;
    for (int i = 0; i < point.width; ++i) {
        const int index = (point.endian == EndianCode::LittleEndian ||
                           point.endian == EndianCode::LittleEndianByteSwap)
                              ? point.width - i - 1
                              : i;
        auto word = registers[index];
        if (point.endian >= EndianCode::BigEndianByteSwap)
            word = static_cast<std::uint16_t>((word >> enum_value(Layout::BitsPerByte)) |
                                              (word << enum_value(Layout::BitsPerByte)));
        bits = (bits << enum_value(Layout::BitsPerRegister)) | word;
    }
    switch (point.decode) {
        case DecodeCode::INT16:
            return static_cast<std::int16_t>(bits);
        case DecodeCode::UINT32:
            return static_cast<std::uint32_t>(bits);
        case DecodeCode::INT32:
            return static_cast<std::int32_t>(bits);
        case DecodeCode::FLOAT: {
            const auto raw = static_cast<std::uint32_t>(bits);
            float value;
            std::memcpy(&value, &raw, sizeof(value));
            if (!std::isfinite(value)) throw std::runtime_error("Non-finite FLOAT sample");
            return value;
        }
        case DecodeCode::DOUBLE: {
            double value;
            std::memcpy(&value, &bits, sizeof(value));
            if (!std::isfinite(value)) throw std::runtime_error("Non-finite DOUBLE sample");
            return value;
        }
        case DecodeCode::UINT8:
            return static_cast<std::uint8_t>(bits);
        case DecodeCode::INT8:
            return static_cast<std::int8_t>(bits);
        case DecodeCode::BOOL:
            return bits != 0;
        case DecodeCode::BIT:
            return ((bits >> point.bit) & 1) != 0;
        default:
            return static_cast<std::uint16_t>(bits);
    }
}

std::array<std::uint16_t, enum_value(Layout::QuadRegisters)> encode(const Point& point,
                                                                    double value) {
    if (!std::isfinite(value)) throw std::invalid_argument("Write value must be finite");
    std::uint64_t bits = 0;
    if (point.decode == DecodeCode::FLOAT) {
        const float real = static_cast<float>(value);
        if (!std::isfinite(real)) throw std::invalid_argument("FLOAT write value overflows");
        std::uint32_t raw;
        std::memcpy(&raw, &real, sizeof(raw));
        bits = raw;
    } else if (point.decode == DecodeCode::DOUBLE) {
        std::memcpy(&bits, &value, sizeof(bits));
    } else {
        static constexpr double minimum[] = {0, std::numeric_limits<std::int16_t>::min(),
                                             0, std::numeric_limits<std::int32_t>::min(),
                                             0, 0,
                                             0, std::numeric_limits<std::int8_t>::min(),
                                             0, 0};
        static constexpr double maximum[] = {std::numeric_limits<std::uint16_t>::max(),
                                             std::numeric_limits<std::int16_t>::max(),
                                             std::numeric_limits<std::uint32_t>::max(),
                                             std::numeric_limits<std::int32_t>::max(),
                                             0,
                                             0,
                                             std::numeric_limits<std::uint8_t>::max(),
                                             std::numeric_limits<std::int8_t>::max(),
                                             1,
                                             1};
        if (std::trunc(value) != value || value < minimum[enum_value(point.decode)] ||
            value > maximum[enum_value(point.decode)])
            throw std::invalid_argument("Integer write is out of range");
        bits = static_cast<std::uint64_t>(static_cast<std::int64_t>(value));
    }
    std::array<std::uint16_t, enum_value(Layout::QuadRegisters)> registers{};
    for (int i = 0; i < point.width; ++i) {
        auto word = static_cast<std::uint16_t>(
            bits >> (enum_value(Layout::BitsPerRegister) * (point.width - i - 1)));
        if (point.endian >= EndianCode::BigEndianByteSwap)
            word = static_cast<std::uint16_t>((word >> enum_value(Layout::BitsPerByte)) |
                                              (word << enum_value(Layout::BitsPerByte)));
        const int index = (point.endian == EndianCode::LittleEndian ||
                           point.endian == EndianCode::LittleEndianByteSwap)
                              ? point.width - i - 1
                              : i;
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
    int port{enum_value(Defaults::TcpPort)}, baud{enum_value(SerialDefaults::BaudRate)},
        data_bits{enum_value(SerialDefaults::DataBits)},
        stop_bits{enum_value(SerialDefaults::StopBits)}, slave{enum_value(Defaults::SlaveId)},
        timeout{enum_value(Defaults::ResponseTimeoutMs)};
    int image_size{enum_value(Defaults::ImageSize)},
        max_sessions{enum_value(Defaults::MaxSessions)}, listener{-1};
    char parity{'N'};
    std::vector<int> slaves{enum_value(Defaults::SlaveId)};

    Context make_context() const {
        Context result(rtu ? modbus_new_rtu(endpoint.c_str(), baud, parity, data_bits, stop_bits)
                           : modbus_new_tcp(endpoint.c_str(), port));
        if (!result) throw std::runtime_error(modbus_strerror(errno));
        modbus_set_slave(result.get(), slave);
        modbus_set_response_timeout(result.get(),
                                    timeout / enum_value(TimeUnits::MillisecondsPerSecond),
                                    (timeout % enum_value(TimeUnits::MillisecondsPerSecond)) *
                                        enum_value(TimeUnits::MicrosecondsPerMillisecond));
        modbus_set_byte_timeout(result.get(), 0, enum_value(Defaults::IoWaitUs));
        modbus_set_indication_timeout(result.get(), 0, enum_value(Defaults::IoWaitUs));
        return result;
    }

    void respond(modbus_t* peer, const std::uint8_t* request, int length) {
        const int header = modbus_get_header_length(peer);
        if (length <= header) return;
        const auto function = static_cast<FunctionCode>(request[header]);
        if (function < FunctionCode::ReadCoils || function > FunctionCode::ReadInputRegisters) {
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
                timeval wait{0, enum_value(Defaults::IoWaitUs)};
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
            if (it == mappings.end() || point.address + count > image_size)
                return enum_value(OperationResult::Failure);
            const auto* mapping = it->second.get();
            for (int i = 0; i < count; ++i) {
                switch (point.function) {
                    case FunctionCode::ReadCoils:
                        registers[i] = mapping->tab_bits[point.address + i];
                        break;
                    case FunctionCode::ReadDiscreteInputs:
                        registers[i] = mapping->tab_input_bits[point.address + i];
                        break;
                    case FunctionCode::ReadHoldingRegisters:
                        registers[i] = mapping->tab_registers[point.address + i];
                        break;
                    case FunctionCode::ReadInputRegisters:
                        registers[i] = mapping->tab_input_registers[point.address + i];
                        break;
                }
            }
            return count;
        }
        modbus_set_slave(context.get(), point.slave);
        if (point.function <= FunctionCode::ReadDiscreteInputs) {
            std::array<std::uint8_t, enum_value(Limits::MaxReadCoils)> bits{};
            const int result =
                point.function == FunctionCode::ReadCoils
                    ? modbus_read_bits(context.get(), point.address, count, bits.data())
                    : modbus_read_input_bits(context.get(), point.address, count, bits.data());
            if (result == count)
                for (int i = 0; i < count; ++i) registers[i] = bits[i];
            return result;
        }
        return point.function == FunctionCode::ReadHoldingRegisters
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
        return enum_value(OperationResult::Failure);
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
            config["slave_id"] =
                config["rtu_addr"].is_string()
                    ? integer(config["rtu_addr"].get<std::string>(), enum_value(Limits::MaxSlaveId))
                    : config["rtu_addr"].get<int>();
        if (!config.contains("timeout_ms") && config.contains("timeout")) {
            const auto seconds = config["timeout"].get<std::int64_t>();
            if (seconds < 1 || seconds > enum_value(Limits::MaxLegacyTimeoutSeconds))
                throw std::invalid_argument("Legacy timeout seconds out of range");
            config["timeout_ms"] = seconds * enum_value(TimeUnits::MillisecondsPerSecond);
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
        impl_->port = config.value("port", enum_value(Defaults::TcpPort));
        impl_->endpoint = config.value("host", config.value("ip", std::string("127.0.0.1")));
        if (config.contains("connection") && !config.contains("host") && !config.contains("ip")) {
            impl_->endpoint = config.at("connection").get<std::string>();
            if (!impl_->rtu) {
                const auto colon = impl_->endpoint.rfind(':');
                if (colon != std::string::npos) {
                    impl_->port =
                        integer(impl_->endpoint.substr(colon + 1), enum_value(Limits::MaxTcpPort));
                    impl_->endpoint.resize(colon);
                }
            }
        }
        if (impl_->rtu) impl_->endpoint = config.value("serial_port", impl_->endpoint);
        impl_->baud =
            impl_->rtu
                ? config.value("baudrate",
                               config.value("baud_rate", enum_value(SerialDefaults::BaudRate)))
                : enum_value(SerialDefaults::BaudRate);
        impl_->data_bits = impl_->rtu
                               ? config.value("data_bits", enum_value(SerialDefaults::DataBits))
                               : enum_value(SerialDefaults::DataBits);
        impl_->stop_bits =
            impl_->rtu
                ? config.value("stopbits",
                               config.value("stop_bits", enum_value(SerialDefaults::StopBits)))
                : enum_value(SerialDefaults::StopBits);
        const auto parity =
            impl_->rtu ? config.value("parity", std::string("N")) : std::string("N");
        if (parity.size() != 1 || std::string("NEO").find(parity[0]) == std::string::npos)
            throw std::invalid_argument("Parity must be N, E or O");
        impl_->parity = parity[0];
        impl_->slave = config.value("slave_id", enum_value(Defaults::SlaveId));
        impl_->timeout = config.value("timeout_ms", enum_value(Defaults::ResponseTimeoutMs));
        impl_->image_size = config.value("image_size", enum_value(Defaults::ImageSize));
        impl_->max_sessions = config.value("max_sessions", enum_value(Defaults::MaxSessions));
        impl_->slaves = config.value("slaves", std::vector<int>{impl_->slave});
        if (impl_->endpoint.empty() || impl_->port < 1 ||
            impl_->port > enum_value(Limits::MaxTcpPort) || impl_->baud <= 0 ||
            impl_->data_bits < enum_value(SerialLimits::MinDataBits) ||
            impl_->data_bits > enum_value(SerialLimits::MaxDataBits) || impl_->stop_bits < 1 ||
            impl_->stop_bits > enum_value(SerialLimits::MaxStopBits) || impl_->slave < 1 ||
            impl_->slave > enum_value(Limits::MaxSlaveId) || impl_->timeout <= 0 ||
            impl_->timeout > enum_value(Limits::MaxTimeoutMs) || impl_->image_size < 1 ||
            impl_->image_size > enum_value(Limits::AddressCount) || impl_->max_sessions < 1 ||
            impl_->max_sessions > FD_SETSIZE - 1 || impl_->slaves.empty() ||
            impl_->slaves.size() > enum_value(Limits::MaxSlaveId))
            throw std::invalid_argument("Modbus configuration is out of range");
        for (int unit : impl_->slaves)
            if (unit < 1 || unit > enum_value(Limits::MaxSlaveId))
                throw std::invalid_argument("Slave is out of range");
        if (impl_->rtu && impl_->server &&
            (impl_->slaves.size() != 1 || impl_->slaves[0] != impl_->slave))
            throw std::invalid_argument(
                "RTU server supports one configured slave per serial resource");
        impl_->initialized = true;
        impl_->error.clear();
        return enum_value(OperationResult::Success);
    } catch (const std::exception& error) {
        impl_->error = error.what();
        return enum_value(OperationResult::Failure);
    }
}

int ModbusProtocol::connect() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->running) return enum_value(OperationResult::Success);
    if (!impl_->initialized || impl_->worker.joinable()) {
        impl_->error = "Initialize (or disconnect stopped worker) before connecting";
        return enum_value(OperationResult::Failure);
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
        return enum_value(OperationResult::Success);
    } catch (const std::exception& error) {
        impl_->error = error.what();
        impl_->running.store(false);
        close_socket(impl_->listener);
        impl_->listener = -1;
        impl_->context.reset();
        impl_->mappings.clear();
        return enum_value(OperationResult::Failure);
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
    return enum_value(OperationResult::Success);
}

bool ModbusProtocol::is_connected() const { return impl_->running.load(); }

int ModbusProtocol::read_batch(const std::vector<std::string>& codes, std::string& output) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    try {
        if (!impl_->running) throw std::runtime_error("Modbus instance is disconnected");
        if (codes.size() > enum_value(PluginLimits::MaxBatchPoints))
            throw std::invalid_argument("Batch exceeds point budget");
        std::vector<Point> points;
        Json results = Json::array();
        for (std::size_t i = 0; i < codes.size(); ++i) {
            auto point = parse(codes[i], false);
            point.index = i;
            points.push_back(point);
            results.push_back({{"code", codes[i]},
                               {"value", nullptr},
                               {"raw_value", nullptr},
                               {"quality", enum_value(SampleQuality::Error)},
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
            const int limit = begin.function <= FunctionCode::ReadDiscreteInputs
                                  ? enum_value(Limits::MaxReadCoils)
                                  : enum_value(Limits::MaxReadRegisters);
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
            std::array<std::uint16_t, enum_value(Limits::MaxReadCoils)> registers{};
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
                    const Json value = point.function <= FunctionCode::ReadDiscreteInputs
                                           ? Json(data[0] != 0)
                                           : decode(point, data);
                    results[point.index]["value"] = value;
                    results[point.index]["raw_value"] = value;
                    results[point.index]["quality"] = enum_value(SampleQuality::Good);
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
        return enum_value(OperationResult::Failure);
    }
}

int ModbusProtocol::write(const std::string& code, double value) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    try {
        if (!impl_->running) throw std::runtime_error("Modbus instance is disconnected");
        auto point = parse(code, true);
        if (!impl_->server && (point.function == FunctionCode::ReadDiscreteInputs ||
                               point.function == FunctionCode::ReadInputRegisters))
            throw std::invalid_argument("Cannot write a read-only client address space");
        if (point.decode == DecodeCode::BIT)
            throw std::invalid_argument("Register bit writes require a dedicated policy");
        const bool coil = point.function == FunctionCode::ReadCoils ||
                          point.function == FunctionCode::ReadDiscreteInputs ||
                          point.function == FunctionCode::WriteSingleCoil ||
                          point.function == FunctionCode::WriteMultipleCoils;
        if (coil && value != 0 && value != 1)
            throw std::invalid_argument("Coil value must be 0 or 1");
        if (!impl_->server && point.function == FunctionCode::WriteSingleRegister &&
            point.width != 1)
            throw std::invalid_argument("Function 6 accepts one register");
        const auto data = encode(point, value);
        if (impl_->server) {
            const auto it = impl_->mappings.find(point.slave);
            if (it == impl_->mappings.end() || point.address + point.width > impl_->image_size)
                throw std::invalid_argument("Point outside server image");
            auto* mapping = it->second.get();
            if (coil) {
                auto* bits = point.function == FunctionCode::ReadDiscreteInputs
                                 ? mapping->tab_input_bits
                                 : mapping->tab_bits;
                bits[point.address] = value != 0;
            } else {
                auto* registers = point.function == FunctionCode::ReadInputRegisters
                                      ? mapping->tab_input_registers
                                      : mapping->tab_registers;
                std::copy_n(data.begin(), point.width, registers + point.address);
            }
        } else {
            modbus_set_slave(impl_->context.get(), point.slave);
            const std::uint8_t bit = value != 0;
            const int result =
                coil ? (point.function == FunctionCode::WriteMultipleCoils
                            ? modbus_write_bits(impl_->context.get(), point.address, 1, &bit)
                            : modbus_write_bit(impl_->context.get(), point.address, bit))
                     : (point.width == 1 && point.function != FunctionCode::WriteMultipleRegisters
                            ? modbus_write_register(impl_->context.get(), point.address, data[0])
                            : modbus_write_registers(impl_->context.get(), point.address,
                                                     point.width, data.data()));
            if (result != (coil ? 1 : point.width)) {
                if (errno < MODBUS_ENOBASE) impl_->running = false;
                throw std::runtime_error(modbus_strerror(errno));
            }
        }
        impl_->error.clear();
        return enum_value(OperationResult::Success);
    } catch (const std::exception& error) {
        impl_->error = error.what();
        return enum_value(OperationResult::Failure);
    }
}

std::string ModbusProtocol::last_error() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->error;
}

}  // namespace ems::net::protocol
