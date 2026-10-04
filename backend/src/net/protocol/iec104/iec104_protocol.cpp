#include "net/protocol/iec104_protocol.h"

#include <cs104_connection.h>
#include <cs104_slave.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <tuple>

namespace ems::net::protocol {
namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

const std::map<std::string, int> kTypes = {
    {"M_SP_NA_1", 1},  {"M_SP_TA_1", 2},  {"M_DP_NA_1", 3},  {"M_DP_TA_1", 4},  {"M_ME_NA_1", 9},
    {"M_ME_TA_1", 10}, {"M_ME_NB_1", 11}, {"M_ME_TB_1", 12}, {"M_ME_NC_1", 13}, {"M_ME_TC_1", 14},
    {"M_ME_ND_1", 21}, {"M_SP_TB_1", 30}, {"M_DP_TB_1", 31}, {"M_ME_TD_1", 34}, {"M_ME_TE_1", 35},
    {"M_ME_TF_1", 36}, {"C_SC_NA_1", 45}, {"C_DC_NA_1", 46}, {"C_RC_NA_1", 47}, {"C_SE_NA_1", 48},
    {"C_SE_NB_1", 49}, {"C_SE_NC_1", 50}, {"C_SC_TA_1", 58}, {"C_DC_TA_1", 59}, {"C_RC_TA_1", 60},
    {"C_SE_TA_1", 61}, {"C_SE_TB_1", 62}, {"C_SE_TC_1", 63}};

int number(const std::string& text, int low, int high) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("Expected an unsigned decimal integer");
    const auto value = std::stoll(text);
    if (value < low || value > high) throw std::invalid_argument("Address/type out of range");
    return static_cast<int>(value);
}

struct Address {
    int ca, ioa, type;

    auto key() const { return std::make_tuple(ca, ioa, type); }
};

Address parse(const std::string& code) {
    const auto first = code.find(':');
    const auto second = code.find(':', first == std::string::npos ? 0 : first + 1);
    if (first == std::string::npos || second == std::string::npos ||
        code.find(':', second + 1) != std::string::npos)
        throw std::invalid_argument("IEC104 point code must be CA:IOA:ASDU");
    const auto name = code.substr(second + 1);
    const auto entry = kTypes.find(name);
    const int type = entry == kTypes.end() ? number(name, 1, 255) : entry->second;
    if (std::none_of(kTypes.begin(), kTypes.end(),
                     [&](const auto& item) { return item.second == type; }))
        throw std::invalid_argument("Unsupported IEC104 ASDU type");
    return {number(code.substr(0, first), 1, 65534),
            number(code.substr(first + 1, second - first - 1), 0, 0xffffff), type};
}

bool control(int type) { return (type >= 45 && type <= 50) || (type >= 58 && type <= 63); }

int family(int type) {
    switch (type) {
        case 1:
        case 2:
        case 30:
        case 45:
        case 58:
            return 1;
        case 3:
        case 4:
        case 31:
        case 46:
        case 59:
            return 2;
        case 47:
        case 60:
            return 3;
        case 9:
        case 10:
        case 21:
        case 34:
        case 48:
        case 61:
            return 4;
        case 11:
        case 12:
        case 35:
        case 49:
        case 62:
            return 5;
        default:
            return 6;
    }
}

void validate_value(int type, double value) {
    if (!std::isfinite(value)) throw std::invalid_argument("Value must be finite");
    const int kind = family(type);
    if (kind <= 3 || kind == 5) {
        const double low = kind == 1 ? 0 : kind <= 3 ? (control(type) ? 1 : 0) : -32768;
        const double high = kind == 1 ? 1 : kind <= 3 ? (control(type) ? 2 : 3) : 32767;
        if (std::trunc(value) != value || value < low || value > high)
            throw std::invalid_argument("Integer/state value outside ASDU range");
    } else if (kind == 4) {
        if (value < -1 || value > 32767.0 / 32768.0)
            throw std::invalid_argument("Normalized value outside [-1,32767/32768]");
    } else if (std::abs(value) > std::numeric_limits<float>::max()) {
        throw std::invalid_argument("Short float overflow");
    }
}

struct Sample {
    double value{0};
    int quality{0};
    std::int64_t timestamp{0};
    Clock::time_point received{Clock::now()};
    bool device_time{false};
    bool invalid_time{false};
};

using ObjectPtr = std::unique_ptr<sInformationObject, decltype(&InformationObject_destroy)>;
using AsduPtr = std::unique_ptr<sCS101_ASDU, decltype(&CS101_ASDU_destroy)>;

bool valid_cp56(CP56Time2a timestamp) {
    const int month = CP56Time2a_getMonth(timestamp);
    const int year = CP56Time2a_getYear(timestamp);
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12 || year > 99) return false;
    const int day = CP56Time2a_getDayOfMonth(timestamp);
    const int maximum = days[month - 1] + (month == 2 && year % 4 == 0 ? 1 : 0);
    return day >= 1 && day <= maximum && CP56Time2a_getHour(timestamp) < 24 &&
           CP56Time2a_getMinute(timestamp) < 60 && CP56Time2a_getSecond(timestamp) < 60 &&
           CP56Time2a_getMillisecond(timestamp) < 1000;
}

InformationObject monitoring_object(const Address& address, const Sample& sample, int time_offset) {
    const auto quality = static_cast<QualityDescriptor>(sample.quality);
    sCP56Time2a time56{};
    sCP24Time2a time24{};
    CP56Time2a_createFromMsTimestamp(
        &time56, static_cast<std::uint64_t>(sample.timestamp + time_offset * 60000LL));
    CP56Time2a_setInvalid(&time56, sample.invalid_time);
    CP24Time2a_setMinute(&time24, CP56Time2a_getMinute(&time56));
    CP24Time2a_setSecond(&time24, CP56Time2a_getSecond(&time56));
    CP24Time2a_setMillisecond(&time24, CP56Time2a_getMillisecond(&time56));
    CP24Time2a_setInvalid(&time24, sample.invalid_time);
    const int ioa = address.ioa;
    const float value = static_cast<float>(sample.value);
    const int integer = family(address.type) <= 5 ? static_cast<int>(sample.value) : 0;
    switch (address.type) {
        case 1:
            return (InformationObject)SinglePointInformation_create(nullptr, ioa, integer != 0,
                                                                    quality);
        case 2:
            return (InformationObject)SinglePointWithCP24Time2a_create(nullptr, ioa, integer != 0,
                                                                       quality, &time24);
        case 30:
            return (InformationObject)SinglePointWithCP56Time2a_create(nullptr, ioa, integer != 0,
                                                                       quality, &time56);
        case 3:
            return (InformationObject)DoublePointInformation_create(
                nullptr, ioa, (DoublePointValue)integer, quality);
        case 4:
            return (InformationObject)DoublePointWithCP24Time2a_create(
                nullptr, ioa, (DoublePointValue)integer, quality, &time24);
        case 31:
            return (InformationObject)DoublePointWithCP56Time2a_create(
                nullptr, ioa, (DoublePointValue)integer, quality, &time56);
        case 9:
            return (InformationObject)MeasuredValueNormalized_create(nullptr, ioa, value, quality);
        case 10:
            return (InformationObject)MeasuredValueNormalizedWithCP24Time2a_create(
                nullptr, ioa, value, quality, &time24);
        case 21:
            return (InformationObject)MeasuredValueNormalizedWithoutQuality_create(nullptr, ioa,
                                                                                   value);
        case 34:
            return (InformationObject)MeasuredValueNormalizedWithCP56Time2a_create(
                nullptr, ioa, value, quality, &time56);
        case 11:
            return (InformationObject)MeasuredValueScaled_create(nullptr, ioa, integer, quality);
        case 12:
            return (InformationObject)MeasuredValueScaledWithCP24Time2a_create(
                nullptr, ioa, integer, quality, &time24);
        case 35:
            return (InformationObject)MeasuredValueScaledWithCP56Time2a_create(
                nullptr, ioa, integer, quality, &time56);
        case 13:
            return (InformationObject)MeasuredValueShort_create(nullptr, ioa, value, quality);
        case 14:
            return (InformationObject)MeasuredValueShortWithCP24Time2a_create(nullptr, ioa, value,
                                                                              quality, &time24);
        case 36:
            return (InformationObject)MeasuredValueShortWithCP56Time2a_create(nullptr, ioa, value,
                                                                              quality, &time56);
        default:
            return nullptr;
    }
}

Sample decode_monitoring(int type, InformationObject object, int time_offset) {
    Sample sample;
    sample.timestamp = now_ms();
    CP56Time2a timestamp = nullptr;
    CP24Time2a short_time = nullptr;
    switch (family(type)) {
        case 1:
            sample.value = SinglePointInformation_getValue((SinglePointInformation)object) ? 1 : 0;
            sample.quality = SinglePointInformation_getQuality((SinglePointInformation)object);
            break;
        case 2:
            sample.value = DoublePointInformation_getValue((DoublePointInformation)object);
            sample.quality = DoublePointInformation_getQuality((DoublePointInformation)object);
            break;
        case 4:
            sample.value = MeasuredValueNormalized_getValue((MeasuredValueNormalized)object);
            sample.quality =
                type == 21 ? 0
                           : MeasuredValueNormalized_getQuality((MeasuredValueNormalized)object);
            break;
        case 5:
            sample.value = MeasuredValueScaled_getValue((MeasuredValueScaled)object);
            sample.quality = MeasuredValueScaled_getQuality((MeasuredValueScaled)object);
            break;
        default:
            sample.value = MeasuredValueShort_getValue((MeasuredValueShort)object);
            sample.quality = MeasuredValueShort_getQuality((MeasuredValueShort)object);
            break;
    }
    switch (type) {
        case 30:
            timestamp = SinglePointWithCP56Time2a_getTimestamp((SinglePointWithCP56Time2a)object);
            break;
        case 31:
            timestamp = DoublePointWithCP56Time2a_getTimestamp((DoublePointWithCP56Time2a)object);
            break;
        case 34:
            timestamp = MeasuredValueNormalizedWithCP56Time2a_getTimestamp(
                (MeasuredValueNormalizedWithCP56Time2a)object);
            break;
        case 35:
            timestamp = MeasuredValueScaledWithCP56Time2a_getTimestamp(
                (MeasuredValueScaledWithCP56Time2a)object);
            break;
        case 36:
            timestamp = MeasuredValueShortWithCP56Time2a_getTimestamp(
                (MeasuredValueShortWithCP56Time2a)object);
            break;
        case 2:
            short_time = SinglePointWithCP24Time2a_getTimestamp((SinglePointWithCP24Time2a)object);
            break;
        case 4:
            short_time = DoublePointWithCP24Time2a_getTimestamp((DoublePointWithCP24Time2a)object);
            break;
        case 10:
            short_time = MeasuredValueNormalizedWithCP24Time2a_getTimestamp(
                (MeasuredValueNormalizedWithCP24Time2a)object);
            break;
        case 12:
            short_time = MeasuredValueScaledWithCP24Time2a_getTimestamp(
                (MeasuredValueScaledWithCP24Time2a)object);
            break;
        case 14:
            short_time = MeasuredValueShortWithCP24Time2a_getTimestamp(
                (MeasuredValueShortWithCP24Time2a)object);
            break;
    }
    if (timestamp) {
        sample.invalid_time = CP56Time2a_isInvalid(timestamp) || !valid_cp56(timestamp);
        if (!sample.invalid_time) {
            sample.timestamp = static_cast<std::int64_t>(CP56Time2a_toMsTimestamp(timestamp)) -
                               time_offset * 60000LL;
            sample.device_time = true;
        }
    }
    // CP24 缺少日期/小时，保留接收时刻，不猜测设备时间。
    if (short_time)
        sample.invalid_time =
            CP24Time2a_isInvalid(short_time) || CP24Time2a_getMinute(short_time) >= 60 ||
            CP24Time2a_getSecond(short_time) >= 60 || CP24Time2a_getMillisecond(short_time) >= 1000;
    return sample;
}

InformationObject command_object(const Address& address, double value, bool select, int qualifier,
                                 int time_offset) {
    sCP56Time2a timestamp{};
    CP56Time2a_createFromMsTimestamp(&timestamp,
                                     static_cast<std::uint64_t>(now_ms() + time_offset * 60000LL));
    const int ioa = address.ioa;
    const int integer = family(address.type) <= 5 ? static_cast<int>(value) : 0;
    const float floating = static_cast<float>(value);
    switch (address.type) {
        case 45:
            return (InformationObject)SingleCommand_create(nullptr, ioa, integer != 0, select,
                                                           qualifier);
        case 46:
            return (InformationObject)DoubleCommand_create(nullptr, ioa, integer, select,
                                                           qualifier);
        case 47:
            return (InformationObject)StepCommand_create(nullptr, ioa, (StepCommandValue)integer,
                                                         select, qualifier);
        case 48:
            return (InformationObject)SetpointCommandNormalized_create(nullptr, ioa, floating,
                                                                       select, qualifier);
        case 49:
            return (InformationObject)SetpointCommandScaled_create(nullptr, ioa, integer, select,
                                                                   qualifier);
        case 50:
            return (InformationObject)SetpointCommandShort_create(nullptr, ioa, floating, select,
                                                                  qualifier);
        case 58:
            return (InformationObject)SingleCommandWithCP56Time2a_create(
                nullptr, ioa, integer != 0, select, qualifier, &timestamp);
        case 59:
            return (InformationObject)DoubleCommandWithCP56Time2a_create(
                nullptr, ioa, integer, select, qualifier, &timestamp);
        case 60:
            return (InformationObject)StepCommandWithCP56Time2a_create(
                nullptr, ioa, (StepCommandValue)integer, select, qualifier, &timestamp);
        case 61:
            return (InformationObject)SetpointCommandNormalizedWithCP56Time2a_create(
                nullptr, ioa, floating, select, qualifier, &timestamp);
        case 62:
            return (InformationObject)SetpointCommandScaledWithCP56Time2a_create(
                nullptr, ioa, integer, select, qualifier, &timestamp);
        case 63:
            return (InformationObject)SetpointCommandShortWithCP56Time2a_create(
                nullptr, ioa, floating, select, qualifier, &timestamp);
        default:
            return nullptr;
    }
}

std::tuple<double, bool, int> command_value(int type, InformationObject object) {
    switch (family(type)) {
        case 1:
            return {SingleCommand_getState((SingleCommand)object) ? 1.0 : 0.0,
                    SingleCommand_isSelect((SingleCommand)object),
                    SingleCommand_getQU((SingleCommand)object)};
        case 2:
            return {DoubleCommand_getState((DoubleCommand)object),
                    DoubleCommand_isSelect((DoubleCommand)object),
                    DoubleCommand_getQU((DoubleCommand)object)};
        case 3:
            return {StepCommand_getState((StepCommand)object),
                    StepCommand_isSelect((StepCommand)object),
                    StepCommand_getQU((StepCommand)object)};
        case 4:
            return {SetpointCommandNormalized_getValue((SetpointCommandNormalized)object),
                    SetpointCommandNormalized_isSelect((SetpointCommandNormalized)object),
                    SetpointCommandNormalized_getQL((SetpointCommandNormalized)object)};
        case 5:
            return {SetpointCommandScaled_getValue((SetpointCommandScaled)object),
                    SetpointCommandScaled_isSelect((SetpointCommandScaled)object),
                    SetpointCommandScaled_getQL((SetpointCommandScaled)object)};
        default:
            return {SetpointCommandShort_getValue((SetpointCommandShort)object),
                    SetpointCommandShort_isSelect((SetpointCommandShort)object),
                    SetpointCommandShort_getQL((SetpointCommandShort)object)};
    }
}

int option_int(const Json& config, const char* key, int fallback, int low, int high) {
    if (!config.contains(key)) return fallback;
    const auto& field = config.at(key);
    if (!field.is_number_integer())
        throw std::invalid_argument(std::string(key) + " must be integer");
    if (field.is_number_unsigned() && field.get<std::uint64_t>() > static_cast<std::uint64_t>(high))
        throw std::invalid_argument(std::string(key) + " out of range");
    const auto value = field.get<std::int64_t>();
    if (value < low || value > high)
        throw std::invalid_argument(std::string(key) + " out of range");
    return static_cast<int>(value);
}
}  // namespace

class Iec104Protocol::Impl {
   public:
    mutable std::mutex mutex;
    std::condition_variable cv;
    CS104_Connection connection{nullptr};
    CS104_Slave server{nullptr};
    bool configured{false}, server_role{false}, ready{false}, opened{false}, closed{true};
    bool initial_gi{true}, select_execute{false}, require_termination{true};
    std::string host, error;
    int port{2404}, timeout_ms{3000}, command_timeout_ms{3000}, stale_ms{30000};
    int originator{0}, max_points{65536}, max_sessions{8}, queue_size{1024}, qualifier{0};
    int time_offset{0};
    sCS104_APCIParameters apci{12, 8, 10, 15, 10, 20};
    std::set<int> stations;
    std::map<std::tuple<int, int, int>, Sample> samples;

    struct Pending {
        bool active{false}, confirmed{false}, done{false}, rejected{false};
        Address address{0, 0, 0};
        std::tuple<double, bool, int> value{0, false, 0};
    } pending;

    void set_error(std::string text) {
        std::lock_guard<std::mutex> lock(mutex);
        error = std::move(text);
    }

    void stop() {
        // 销毁会等待栈线程；不能持有回调使用的 mutex，也不能在回调中调用。
        if (connection) {
            CS104_Connection_destroy(connection);
            connection = nullptr;
        }
        if (server) {
            CS104_Slave_stop(server);
            CS104_Slave_destroy(server);
            server = nullptr;
        }
        std::lock_guard<std::mutex> lock(mutex);
        ready = false;
        opened = false;
        closed = true;
        pending = {};
        if (!server_role) samples.clear();
        cv.notify_all();
    }

    static void connection_event(void* parameter, CS104_Connection,
                                 CS104_ConnectionEvent event) noexcept {
        auto* self = static_cast<Impl*>(parameter);
        try {
            std::lock_guard<std::mutex> lock(self->mutex);
            if (event == CS104_CONNECTION_OPENED) {
                self->closed = false;
                self->opened = true;
            }
            if (event == CS104_CONNECTION_STARTDT_CON_RECEIVED) self->ready = true;
            if (event == CS104_CONNECTION_STOPDT_CON_RECEIVED) self->ready = false;
            if (event == CS104_CONNECTION_CLOSED || event == CS104_CONNECTION_FAILED) {
                self->ready = false;
                self->opened = false;
                self->closed = true;
                self->samples.clear();
            }
            self->cv.notify_all();
        } catch (...) {
        }
    }

    static bool receive(void* parameter, int, CS101_ASDU asdu) noexcept {
        auto* self = static_cast<Impl*>(parameter);
        try {
            const int ca = CS101_ASDU_getCA(asdu);
            const int type = CS101_ASDU_getTypeID(asdu);
            const auto cot = CS101_ASDU_getCOT(asdu);
            if (CS101_ASDU_isTest(asdu) || !self->stations.count(ca)) return true;
            std::lock_guard<std::mutex> lock(self->mutex);
            if (control(type)) {
                if (!self->pending.active || self->pending.address.ca != ca ||
                    self->pending.address.type != type ||
                    CS101_ASDU_getNumberOfElements(asdu) != 1 ||
                    CS101_ASDU_getOA(asdu) != self->originator)
                    return true;
                ObjectPtr object(CS101_ASDU_getElement(asdu, 0), InformationObject_destroy);
                if (!object ||
                    InformationObject_getObjectAddress(object.get()) != self->pending.address.ioa ||
                    command_value(type, object.get()) != self->pending.value)
                    return true;
                if (cot != CS101_COT_ACTIVATION_CON && cot != CS101_COT_ACTIVATION_TERMINATION)
                    return true;
                if (CS101_ASDU_isNegative(asdu)) {
                    self->pending.rejected = true;
                    self->pending.done = true;
                } else if (cot == CS101_COT_ACTIVATION_CON) {
                    self->pending.confirmed = true;
                    self->pending.done =
                        std::get<1>(self->pending.value) || !self->require_termination;
                } else if (self->pending.confirmed) {
                    self->pending.done = true;
                }
                self->cv.notify_all();
                return true;
            }
            const bool supported = std::any_of(kTypes.begin(), kTypes.end(), [&](const auto& item) {
                return item.second == type && !control(type);
            });
            if (!supported || CS101_ASDU_isNegative(asdu) ||
                !(cot == CS101_COT_PERIODIC || cot == CS101_COT_BACKGROUND_SCAN ||
                  cot == CS101_COT_SPONTANEOUS || cot == CS101_COT_REQUEST ||
                  (cot >= CS101_COT_INTERROGATED_BY_STATION &&
                   cot <= CS101_COT_INTERROGATED_BY_GROUP_16)))
                return true;
            for (int i = 0; i < CS101_ASDU_getNumberOfElements(asdu); ++i) {
                ObjectPtr object(CS101_ASDU_getElement(asdu, i), InformationObject_destroy);
                if (!object) continue;
                const auto key =
                    std::make_tuple(ca, InformationObject_getObjectAddress(object.get()), type);
                if (!self->samples.count(key) &&
                    self->samples.size() >= static_cast<std::size_t>(self->max_points)) {
                    self->error = "IEC104 receive cache full; point discarded";
                    continue;
                }
                self->samples[key] = decode_monitoring(type, object.get(), self->time_offset);
            }
        } catch (...) {
            // C 回调边界不允许异常逃逸；损坏报文不会覆盖其他点。
            return false;
        }
        return true;
    }

    AsduPtr make_asdu(const Address& address, const Sample& sample, CS101_CauseOfTransmission cot,
                      int oa) {
        AsduPtr asdu(CS101_ASDU_create(CS104_Slave_getAppLayerParameters(server), false, cot, oa,
                                       address.ca, false, false),
                     CS101_ASDU_destroy);
        ObjectPtr object(monitoring_object(address, sample, time_offset),
                         InformationObject_destroy);
        if (!asdu || !object || !CS101_ASDU_addInformationObject(asdu.get(), object.get()))
            throw std::runtime_error("Cannot encode IEC104 monitoring ASDU");
        return asdu;
    }

    static bool interrogation(void* parameter, IMasterConnection peer, CS101_ASDU request,
                              std::uint8_t qoi) noexcept {
        auto* self = static_cast<Impl*>(parameter);
        try {
            const int ca = CS101_ASDU_getCA(request);
            if (CS101_ASDU_isTest(request) || CS101_ASDU_getCOT(request) != CS101_COT_ACTIVATION ||
                qoi != IEC60870_QOI_STATION || !self->stations.count(ca)) {
                return IMasterConnection_sendACT_CON(peer, request, true);
            }
            std::vector<std::pair<Address, Sample>> snapshot;
            {
                std::lock_guard<std::mutex> lock(self->mutex);
                for (const auto& item : self->samples) {
                    if (std::get<0>(item.first) == ca)
                        snapshot.push_back(
                            {{ca, std::get<1>(item.first), std::get<2>(item.first)}, item.second});
                }
            }
            if (!IMasterConnection_sendACT_CON(peer, request, false)) return false;
            for (const auto& item : snapshot) {
                auto asdu =
                    self->make_asdu(item.first, item.second, CS101_COT_INTERROGATED_BY_STATION,
                                    CS101_ASDU_getOA(request));
                if (!IMasterConnection_sendASDU(peer, asdu.get())) {
                    self->set_error("General interrogation queue full; ACT_TERM not sent");
                    return false;
                }
            }
            return IMasterConnection_sendACT_TERM(peer, request);
        } catch (...) {
            return false;
        }
    }

    static bool read_point(void* parameter, IMasterConnection peer, CS101_ASDU request,
                           int ioa) noexcept {
        auto* self = static_cast<Impl*>(parameter);
        try {
            std::vector<std::pair<Address, Sample>> snapshot;
            const int ca = CS101_ASDU_getCA(request);
            {
                std::lock_guard<std::mutex> lock(self->mutex);
                for (const auto& item : self->samples) {
                    if (std::get<0>(item.first) == ca && std::get<1>(item.first) == ioa)
                        snapshot.push_back({{ca, ioa, std::get<2>(item.first)}, item.second});
                }
            }
            if (CS101_ASDU_isTest(request) || snapshot.empty()) {
                CS101_ASDU_setCOT(request, self->stations.count(ca) ? CS101_COT_UNKNOWN_IOA
                                                                    : CS101_COT_UNKNOWN_CA);
                CS101_ASDU_setNegative(request, true);
                return IMasterConnection_sendASDU(peer, request);
            }
            for (const auto& item : snapshot) {
                auto asdu = self->make_asdu(item.first, item.second, CS101_COT_REQUEST,
                                            CS101_ASDU_getOA(request));
                if (!IMasterConnection_sendASDU(peer, asdu.get())) return false;
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    static bool reject_control(void*, IMasterConnection peer, CS101_ASDU request) noexcept {
        return IMasterConnection_sendACT_CON(peer, request, true);
    }
};

Iec104Protocol::Iec104Protocol() : impl_(std::make_unique<Impl>()) {}

Iec104Protocol::~Iec104Protocol() { impl_->stop(); }

int Iec104Protocol::initialize(const std::string& text) {
    try {
        if (impl_->connection || impl_->server)
            throw std::invalid_argument("Disconnect before reconfiguration");
        auto config = Json::parse(text);
        if (!config.is_object()) throw std::invalid_argument("Config must be a JSON object");
        if (config.contains("settings")) {
            auto settings = config.at("settings");
            if (settings.is_string())
                settings = settings.get<std::string>().empty()
                               ? Json::object()
                               : Json::parse(settings.get<std::string>());
            if (!settings.is_object() && !settings.is_null())
                throw std::invalid_argument("settings must be object or JSON object string");
            if (settings.is_object())
                for (auto it = settings.begin(); it != settings.end(); ++it)
                    if (!config.contains(it.key())) config[it.key()] = it.value();
        }
        if (config.value("tls", false) || config.value("tls_enabled", false) ||
            config.contains("transport_security"))
            throw std::invalid_argument("TLS is not supported by this IEC104 package");
        if (config.value("protocol", std::string("IEC104")) != "IEC104")
            throw std::invalid_argument("Expected protocol IEC104");
        const int time_offset = option_int(config, "cp56_timezone_offset_minutes", 0, -840, 840);
        const auto role = config.value("local_role", config.value("role", std::string("client")));
        if (role != "client" && role != "server" && role != "server_readonly")
            throw std::invalid_argument("Invalid local_role");
        const bool server_role = role != "client";
        std::string host = config.value(
            "host", config.value("ip", std::string(server_role ? "0.0.0.0" : "127.0.0.1")));
        int port = option_int(config, "port", 2404, 1, 65535);
        if (config.contains("connection") && !config.contains("host") && !config.contains("ip")) {
            const auto endpoint = config.at("connection").get<std::string>();
            const auto colon = endpoint.rfind(':');
            if (colon == std::string::npos)
                host = endpoint;
            else {
                host = endpoint.substr(0, colon);
                if (!config.contains("port")) port = number(endpoint.substr(colon + 1), 1, 65535);
            }
        }
        if (host.empty() || host.size() > 255 || host.find('\0') != std::string::npos ||
            host.find(':') != std::string::npos)
            throw std::invalid_argument("Endpoint must be IPv4 or hostname supported by lib60870");
        std::set<int> stations;
        auto addresses = config.value(
            "common_addresses",
            config.value("slaves",
                         Json::array({option_int(config, "common_address", 1, 1, 65534)})));
        if (!addresses.is_array() || addresses.empty() || addresses.size() > 256)
            throw std::invalid_argument("common_addresses requires 1..256 stations");
        for (const auto& ca : addresses) {
            if (!ca.is_number_integer())
                throw std::invalid_argument("Common address must be integer");
            const auto value = ca.get<std::int64_t>();
            if (value < 1 || value > 65534 || !stations.insert(static_cast<int>(value)).second)
                throw std::invalid_argument("Invalid/duplicate common address");
        }
        const int max_points = option_int(config, "max_points", 65536, 1, 65536);
        std::map<std::tuple<int, int, int>, Sample> samples;
        const auto points = config.value("points", Json::array());
        if (!points.is_array() || points.size() > static_cast<std::size_t>(max_points))
            throw std::invalid_argument("Invalid point image size");
        for (const auto& point : points) {
            const auto address = parse(point.at("code").get<std::string>());
            if (control(address.type) || !stations.count(address.ca))
                throw std::invalid_argument(
                    "Image point must be monitoring type in configured station");
            Sample sample;
            sample.value = point.value("value", 0.0);
            validate_value(address.type, sample.value);
            sample.quality = option_int(point, "quality", 0, 0, 255);
            if ((sample.quality & ~0xf1) || (family(address.type) <= 2 && (sample.quality & 1)) ||
                (address.type == 21 && sample.quality))
                throw std::invalid_argument("Invalid IEC quality bits for ASDU");
            if (point.contains("timestamp") && !point.at("timestamp").is_number_integer())
                throw std::invalid_argument("Point timestamp must be integer milliseconds");
            sample.timestamp = point.value("timestamp", now_ms());
            if (sample.timestamp < 946684800000LL - time_offset * 60000LL ||
                sample.timestamp >= 4102444800000LL - time_offset * 60000LL)
                throw std::invalid_argument("Point timestamp must encode within years 2000..2099");
            if (!samples.emplace(address.key(), sample).second)
                throw std::invalid_argument("Duplicate image point");
        }
        sCS104_APCIParameters apci{};
        apci.k = option_int(config, "send_window_size", 12, 1, 32767);
        apci.w = option_int(config, "receive_window_size", 8, 1, apci.k);
        apci.t0 = option_int(config, "connection_timeout", 10, 1, 60);
        apci.t1 = option_int(config, "message_timeout", 15, 1, 120);
        apci.t2 = option_int(config, "confirm_interval", 10, 1, 120);
        apci.t3 = option_int(config, "keep_alive_interval", 20, 1, 3600);
        if (apci.t2 >= apci.t1)
            throw std::invalid_argument("confirm_interval must be less than message_timeout");
        const int timeout = option_int(config, "timeout_ms", 3000, 1, 60000);
        const int command_timeout = option_int(config, "command_timeout_ms", 3000, 1, 60000);
        const int stale = option_int(config, "stale_ms", 30000, 0, 86400000);
        const int oa = option_int(config, "originator_address", 0, 0, 255);
        const int sessions = option_int(config, "max_connections", 8, 1, 100);
        const int queue = option_int(config, "queue_size", 1024, 16, 65536);
        const int qualifier = option_int(config, "command_qualifier", 0, 0, 3);
        const bool initial_gi = config.value("general_interrogation_on_connect", true);
        const bool select_execute = config.value("select_execute", false);
        const bool termination = config.value("command_require_termination", true);
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->server_role = server_role;
        impl_->host = std::move(host);
        impl_->port = port;
        impl_->stations = std::move(stations);
        impl_->samples = server_role ? std::move(samples) : decltype(samples){};
        impl_->apci = apci;
        impl_->timeout_ms = timeout;
        impl_->command_timeout_ms = command_timeout;
        impl_->stale_ms = stale;
        impl_->originator = oa;
        impl_->time_offset = time_offset;
        impl_->max_points = max_points;
        impl_->max_sessions = sessions;
        impl_->queue_size = queue;
        impl_->qualifier = qualifier;
        impl_->initial_gi = initial_gi;
        impl_->select_execute = select_execute;
        impl_->require_termination = termination;
        impl_->configured = true;
        impl_->error.clear();
        return 0;
    } catch (const std::exception& error) {
        impl_->set_error(error.what());
        return -1;
    }
}

int Iec104Protocol::connect() {
    if (is_connected()) return 0;
    impl_->stop();
    try {
        if (!impl_->configured) throw std::invalid_argument("IEC104 is not initialized");
        if (impl_->server_role) {
            impl_->server = CS104_Slave_create(impl_->queue_size, impl_->queue_size);
            if (!impl_->server) throw std::runtime_error("Cannot create IEC104 server");
            CS104_Slave_setLocalAddress(impl_->server, impl_->host.c_str());
            CS104_Slave_setLocalPort(impl_->server, impl_->port);
            CS104_Slave_setMaxOpenConnections(impl_->server, impl_->max_sessions);
            CS104_Slave_setServerMode(impl_->server, CS104_MODE_CONNECTION_IS_REDUNDANCY_GROUP);
            *CS104_Slave_getConnectionParameters(impl_->server) = impl_->apci;
            CS104_Slave_setInterrogationHandler(impl_->server, Impl::interrogation, impl_.get());
            CS104_Slave_setReadHandler(impl_->server, Impl::read_point, impl_.get());
            CS104_Slave_setASDUHandler(impl_->server, Impl::reject_control, impl_.get());
            CS104_Slave_start(impl_->server);
            if (!CS104_Slave_isRunning(impl_->server))
                throw std::runtime_error("IEC104 listen failed");
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->ready = true;
            impl_->closed = false;
            impl_->error.clear();
            return 0;
        }
        impl_->connection = CS104_Connection_create(impl_->host.c_str(), impl_->port);
        if (!impl_->connection) throw std::runtime_error("Cannot create IEC104 connection");
        CS104_Connection_setAPCIParameters(impl_->connection, &impl_->apci);
        CS104_Connection_setConnectTimeout(impl_->connection, impl_->timeout_ms);
        CS104_Connection_getAppLayerParameters(impl_->connection)->originatorAddress =
            impl_->originator;
        CS104_Connection_setConnectionHandler(impl_->connection, Impl::connection_event,
                                              impl_.get());
        CS104_Connection_setASDUReceivedHandler(impl_->connection, Impl::receive, impl_.get());
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->closed = false;
        }
        const auto deadline = Clock::now() + std::chrono::milliseconds(impl_->timeout_ms);
        CS104_Connection_connectAsync(impl_->connection);
        {
            std::unique_lock<std::mutex> lock(impl_->mutex);
            impl_->cv.wait_until(lock, deadline, [&] { return impl_->opened || impl_->closed; });
            if (!impl_->opened) throw std::runtime_error("IEC104 TCP connect failed or timed out");
            lock.unlock();
            CS104_Connection_sendStartDT(impl_->connection);
            lock.lock();
            impl_->cv.wait_until(lock, deadline, [&] { return impl_->ready || impl_->closed; });
            if (!impl_->ready)
                throw std::runtime_error("IEC104 TCP/STARTDT handshake failed or timed out");
            impl_->error.clear();
        }
        if (impl_->initial_gi && interrogate() != 0)
            throw std::runtime_error("Initial interrogation submission failed");
        return 0;
    } catch (const std::exception& error) {
        impl_->stop();
        impl_->set_error(error.what());
        return -1;
    }
}

int Iec104Protocol::disconnect() {
    impl_->stop();
    return 0;
}

bool Iec104Protocol::is_connected() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->ready && !impl_->closed;
}

std::string Iec104Protocol::last_error() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->error;
}

int Iec104Protocol::interrogate(int ca) {
    if (impl_->server_role || !is_connected() || (ca && !impl_->stations.count(ca))) {
        impl_->set_error("Interrogation requires a ready client and configured station");
        return -1;
    }
    const auto deadline = Clock::now() + std::chrono::milliseconds(impl_->timeout_ms);
    for (const auto station : impl_->stations) {
        if (ca && station != ca) continue;
        while (CS104_Connection_isTransmitBufferFull(impl_->connection) && is_connected() &&
               Clock::now() < deadline) {
            std::unique_lock<std::mutex> lock(impl_->mutex);
            impl_->cv.wait_for(lock, std::chrono::milliseconds(5));
        }
        if (!is_connected() ||
            !CS104_Connection_sendInterrogationCommand(impl_->connection, CS101_COT_ACTIVATION,
                                                       station, IEC60870_QOI_STATION)) {
            impl_->set_error("IEC104 send window full or interrogation send failed");
            return -1;
        }
    }
    return 0;
}

int Iec104Protocol::read_batch(const std::vector<std::string>& codes, std::string& result) {
    if (!is_connected()) {
        impl_->set_error("IEC104 is not ready");
        return -1;
    }
    if (codes.size() > 65536) {
        impl_->set_error("Batch too large");
        return -1;
    }
    Json rows = Json::array();
    int count = 0;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (const auto& code : codes) {
        Json row = {{"code", code}, {"value", nullptr}, {"quality", 1}, {"timestamp", now_ms()}};
        try {
            const auto address = parse(code);
            if (control(address.type))
                throw std::invalid_argument("Monitoring ASDU required for reading");
            const auto found = impl_->samples.find(address.key());
            if (found == impl_->samples.end())
                throw std::invalid_argument("No sample for CA/IOA/type");
            const auto& sample = found->second;
            const bool stale =
                !impl_->server_role && impl_->stale_ms &&
                Clock::now() - sample.received > std::chrono::milliseconds(impl_->stale_ms);
            const bool valid =
                !stale && !sample.quality && !sample.invalid_time && std::isfinite(sample.value) &&
                (family(address.type) != 2 || sample.value == 1 || sample.value == 2);
            row["timestamp"] = sample.timestamp;
            row["iec_quality"] = sample.quality;
            row["timestamp_source"] = impl_->server_role   ? "local_image"
                                      : sample.device_time ? "device_cp56"
                                                           : "received";
            row["stale"] = stale;
            if (valid) {
                row["value"] = sample.value;
                row["quality"] = 0;
                ++count;
            }
        } catch (const std::exception& error) {
            row["error"] = error.what();
        }
        rows.push_back(std::move(row));
    }
    result = rows.dump();
    return count;
}

int Iec104Protocol::write(const std::string& code, double value) {
    try {
        if (!is_connected()) throw std::invalid_argument("IEC104 is not ready");
        const auto address = parse(code);
        if (!impl_->stations.count(address.ca))
            throw std::invalid_argument("Unknown configured station");
        validate_value(address.type, value);
        if (impl_->server_role) {
            if (control(address.type))
                throw std::invalid_argument("Server only updates local monitoring image");
            Sample sample;
            {
                std::lock_guard<std::mutex> lock(impl_->mutex);
                auto found = impl_->samples.find(address.key());
                if (found == impl_->samples.end())
                    throw std::invalid_argument("Point not in server image");
                sample = found->second;
                sample.value = value;
                sample.timestamp = now_ms();
                sample.received = Clock::now();
                found->second = sample;
            }
            auto asdu = impl_->make_asdu(address, sample, CS101_COT_SPONTANEOUS, impl_->originator);
            CS104_Slave_enqueueASDU(impl_->server, asdu.get());
            return 0;
        }
        if (!control(address.type))
            throw std::invalid_argument("Client write requires command ASDU");
        const auto deadline = Clock::now() + std::chrono::milliseconds(impl_->command_timeout_ms);
        for (int phase = impl_->select_execute ? 0 : 1; phase <= 1; ++phase) {
            if (Clock::now() >= deadline) {
                impl_->stop();
                throw std::runtime_error("Command deadline elapsed before send; session closed");
            }
            if (Clock::now() >= deadline) {
                impl_->stop();
                throw std::runtime_error("Command deadline elapsed before send; session closed");
            }
            ObjectPtr object(
                command_object(address, value, phase == 0, impl_->qualifier, impl_->time_offset),
                InformationObject_destroy);
            if (!object) throw std::runtime_error("Cannot encode command");
            {
                std::lock_guard<std::mutex> lock(impl_->mutex);
                impl_->pending = {};
                impl_->pending.active = true;
                impl_->pending.address = address;
                impl_->pending.value = command_value(address.type, object.get());
            }
            if (Clock::now() >= deadline) {
                impl_->stop();
                throw std::runtime_error("Command deadline elapsed before send; session closed");
            }
            if (Clock::now() >= deadline) {
                impl_->stop();
                throw std::runtime_error("Command deadline elapsed before send; session closed");
            }
            if (!CS104_Connection_sendProcessCommandEx(impl_->connection, CS101_COT_ACTIVATION,
                                                       address.ca, object.get())) {
                impl_->stop();
                throw std::runtime_error(
                    "Command send failed; outcome unknown, session closed; do not auto retry");
            }
            std::unique_lock<std::mutex> lock(impl_->mutex);
            impl_->cv.wait_until(lock, deadline,
                                 [&] { return impl_->pending.done || impl_->closed; });
            const bool done = impl_->pending.done;
            const bool rejected = impl_->pending.rejected;
            impl_->pending.active = false;
            if (!done) {
                lock.unlock();
                impl_->stop();
                throw std::runtime_error(
                    "Command timeout/disconnection; outcome unknown, session closed; do not auto "
                    "retry");
            }
            if (rejected) throw std::runtime_error("IEC104 command negatively confirmed");
        }
        return 0;
    } catch (const std::exception& error) {
        impl_->set_error(error.what());
        return -1;
    }
}

}  // namespace ems::net::protocol
