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

#include "net/protocol/iec104/iec104_limits.h"
#include "net/protocol/iec104/iec104_types.h"
#include "net/protocol/protocol_types.h"
#include "net/protocol/serial_types.h"
#include "net/protocol/time_units.h"
#include "net/protocol/traffic_observer.h"

namespace ems::net::protocol {
using iec104::AsduType;
using iec104::Defaults;
using iec104::DoublePointState;
using iec104::Limits;
using iec104::QualityMask;
using iec104::TimeEncoding;
using iec104::ValueFamily;

namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

const std::map<std::string, int> kTypes = {
    {"M_SP_NA_1", enum_value(AsduType::M_SP_NA_1)}, {"M_SP_TA_1", enum_value(AsduType::M_SP_TA_1)},
    {"M_DP_NA_1", enum_value(AsduType::M_DP_NA_1)}, {"M_DP_TA_1", enum_value(AsduType::M_DP_TA_1)},
    {"M_ME_NA_1", enum_value(AsduType::M_ME_NA_1)}, {"M_ME_TA_1", enum_value(AsduType::M_ME_TA_1)},
    {"M_ME_NB_1", enum_value(AsduType::M_ME_NB_1)}, {"M_ME_TB_1", enum_value(AsduType::M_ME_TB_1)},
    {"M_ME_NC_1", enum_value(AsduType::M_ME_NC_1)}, {"M_ME_TC_1", enum_value(AsduType::M_ME_TC_1)},
    {"M_ME_ND_1", enum_value(AsduType::M_ME_ND_1)}, {"M_SP_TB_1", enum_value(AsduType::M_SP_TB_1)},
    {"M_DP_TB_1", enum_value(AsduType::M_DP_TB_1)}, {"M_ME_TD_1", enum_value(AsduType::M_ME_TD_1)},
    {"M_ME_TE_1", enum_value(AsduType::M_ME_TE_1)}, {"M_ME_TF_1", enum_value(AsduType::M_ME_TF_1)},
    {"C_SC_NA_1", enum_value(AsduType::C_SC_NA_1)}, {"C_DC_NA_1", enum_value(AsduType::C_DC_NA_1)},
    {"C_RC_NA_1", enum_value(AsduType::C_RC_NA_1)}, {"C_SE_NA_1", enum_value(AsduType::C_SE_NA_1)},
    {"C_SE_NB_1", enum_value(AsduType::C_SE_NB_1)}, {"C_SE_NC_1", enum_value(AsduType::C_SE_NC_1)},
    {"C_SC_TA_1", enum_value(AsduType::C_SC_TA_1)}, {"C_DC_TA_1", enum_value(AsduType::C_DC_TA_1)},
    {"C_RC_TA_1", enum_value(AsduType::C_RC_TA_1)}, {"C_SE_TA_1", enum_value(AsduType::C_SE_TA_1)},
    {"C_SE_TB_1", enum_value(AsduType::C_SE_TB_1)}, {"C_SE_TC_1", enum_value(AsduType::C_SE_TC_1)}};

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
    const int type =
        entry == kTypes.end() ? number(name, 1, enum_value(Limits::MaxTypeId)) : entry->second;
    if (std::none_of(kTypes.begin(), kTypes.end(),
                     [&](const auto& item) { return item.second == type; }))
        throw std::invalid_argument("Unsupported IEC104 ASDU type");
    return {number(code.substr(0, first), 1, enum_value(Limits::MaxCommonAddress)),
            number(code.substr(first + 1, second - first - 1), 0,
                   enum_value(Limits::MaxInformationAddress)),
            type};
}

bool control(int type) {
    return (type >= enum_value(AsduType::C_SC_NA_1) && type <= enum_value(AsduType::C_SE_NC_1)) ||
           (type >= enum_value(AsduType::C_SC_TA_1) && type <= enum_value(AsduType::C_SE_TC_1));
}

ValueFamily family(int type) {
    switch (static_cast<AsduType>(type)) {
        case AsduType::M_SP_NA_1:
        case AsduType::M_SP_TA_1:
        case AsduType::M_SP_TB_1:
        case AsduType::C_SC_NA_1:
        case AsduType::C_SC_TA_1:
            return ValueFamily::SinglePoint;
        case AsduType::M_DP_NA_1:
        case AsduType::M_DP_TA_1:
        case AsduType::M_DP_TB_1:
        case AsduType::C_DC_NA_1:
        case AsduType::C_DC_TA_1:
            return ValueFamily::DoublePoint;
        case AsduType::C_RC_NA_1:
        case AsduType::C_RC_TA_1:
            return ValueFamily::Step;
        case AsduType::M_ME_NA_1:
        case AsduType::M_ME_TA_1:
        case AsduType::M_ME_ND_1:
        case AsduType::M_ME_TD_1:
        case AsduType::C_SE_NA_1:
        case AsduType::C_SE_TA_1:
            return ValueFamily::Normalized;
        case AsduType::M_ME_NB_1:
        case AsduType::M_ME_TB_1:
        case AsduType::M_ME_TE_1:
        case AsduType::C_SE_NB_1:
        case AsduType::C_SE_TB_1:
            return ValueFamily::Scaled;
        default:
            return ValueFamily::ShortFloat;
    }
}

void validate_value(int type, double value) {
    if (!std::isfinite(value)) throw std::invalid_argument("Value must be finite");
    const auto kind = family(type);
    if (kind <= ValueFamily::Step || kind == ValueFamily::Scaled) {
        const double low = kind == ValueFamily::SinglePoint ? 0
                           : kind <= ValueFamily::Step
                               ? (control(type) ? enum_value(DoublePointState::Off)
                                                : enum_value(DoublePointState::Indeterminate))
                               : std::numeric_limits<std::int16_t>::min();
        const double high = kind == ValueFamily::SinglePoint ? 1
                            : kind <= ValueFamily::Step
                                ? (control(type) ? enum_value(DoublePointState::On)
                                                 : enum_value(DoublePointState::Invalid))
                                : std::numeric_limits<std::int16_t>::max();
        if (std::trunc(value) != value || value < low || value > high)
            throw std::invalid_argument("Integer/state value outside ASDU range");
    } else if (kind == ValueFamily::Normalized) {
        if (value < -1 ||
            value > static_cast<double>(std::numeric_limits<std::int16_t>::max()) /
                        (static_cast<double>(std::numeric_limits<std::int16_t>::max()) + 1))
            throw std::invalid_argument("Normalized value outside [-1,32767/32768]");
    } else if (std::abs(value) > std::numeric_limits<float>::max()) {
        throw std::invalid_argument("Short float overflow");
    }
}

struct Sample {
    double value{0};
    int quality{enum_value(SampleQuality::Good)};
    std::int64_t timestamp{0};
    Clock::time_point received{Clock::now()};
    std::int64_t received_at_ms{now_ms()};
    bool device_time{false};
    bool invalid_time{false};
};

using ObjectPtr = std::unique_ptr<sInformationObject, decltype(&InformationObject_destroy)>;
using AsduPtr = std::unique_ptr<sCS101_ASDU, decltype(&CS101_ASDU_destroy)>;

bool valid_cp56(CP56Time2a timestamp) {
    const int month = CP56Time2a_getMonth(timestamp);
    const int year = CP56Time2a_getYear(timestamp);
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > enum_value(TimeEncoding::MonthsPerYear) ||
        year > enum_value(TimeEncoding::LastYear))
        return false;
    const int day = CP56Time2a_getDayOfMonth(timestamp);
    const int maximum =
        days[month - 1] + (month == enum_value(TimeEncoding::February) &&
                                   year % enum_value(TimeEncoding::LeapYearCycle) == 0
                               ? 1
                               : 0);
    return day >= 1 && day <= maximum &&
           CP56Time2a_getHour(timestamp) < enum_value(TimeUnits::HoursPerDay) &&
           CP56Time2a_getMinute(timestamp) < enum_value(TimeUnits::SecondsPerMinute) &&
           CP56Time2a_getSecond(timestamp) < enum_value(TimeUnits::SecondsPerMinute) &&
           CP56Time2a_getMillisecond(timestamp) < enum_value(TimeUnits::MillisecondsPerSecond);
}

InformationObject monitoring_object(const Address& address, const Sample& sample, int time_offset) {
    const auto quality = static_cast<QualityDescriptor>(sample.quality);
    sCP56Time2a time56{};
    sCP24Time2a time24{};
    CP56Time2a_createFromMsTimestamp(
        &time56, static_cast<std::uint64_t>(sample.timestamp +
                                            time_offset * static_cast<long long>(enum_value(
                                                              TimeUnits::MillisecondsPerMinute))));
    CP56Time2a_setInvalid(&time56, sample.invalid_time);
    CP24Time2a_setMinute(&time24, CP56Time2a_getMinute(&time56));
    CP24Time2a_setSecond(&time24, CP56Time2a_getSecond(&time56));
    CP24Time2a_setMillisecond(&time24, CP56Time2a_getMillisecond(&time56));
    CP24Time2a_setInvalid(&time24, sample.invalid_time);
    const int ioa = address.ioa;
    const float value = static_cast<float>(sample.value);
    const int integer =
        family(address.type) <= ValueFamily::Scaled ? static_cast<int>(sample.value) : 0;
    switch (static_cast<AsduType>(address.type)) {
        case AsduType::M_SP_NA_1:
            return (InformationObject)SinglePointInformation_create(nullptr, ioa, integer != 0,
                                                                    quality);
        case AsduType::M_SP_TA_1:
            return (InformationObject)SinglePointWithCP24Time2a_create(nullptr, ioa, integer != 0,
                                                                       quality, &time24);
        case AsduType::M_SP_TB_1:
            return (InformationObject)SinglePointWithCP56Time2a_create(nullptr, ioa, integer != 0,
                                                                       quality, &time56);
        case AsduType::M_DP_NA_1:
            return (InformationObject)DoublePointInformation_create(
                nullptr, ioa, (DoublePointValue)integer, quality);
        case AsduType::M_DP_TA_1:
            return (InformationObject)DoublePointWithCP24Time2a_create(
                nullptr, ioa, (DoublePointValue)integer, quality, &time24);
        case AsduType::M_DP_TB_1:
            return (InformationObject)DoublePointWithCP56Time2a_create(
                nullptr, ioa, (DoublePointValue)integer, quality, &time56);
        case AsduType::M_ME_NA_1:
            return (InformationObject)MeasuredValueNormalized_create(nullptr, ioa, value, quality);
        case AsduType::M_ME_TA_1:
            return (InformationObject)MeasuredValueNormalizedWithCP24Time2a_create(
                nullptr, ioa, value, quality, &time24);
        case AsduType::M_ME_ND_1:
            return (InformationObject)MeasuredValueNormalizedWithoutQuality_create(nullptr, ioa,
                                                                                   value);
        case AsduType::M_ME_TD_1:
            return (InformationObject)MeasuredValueNormalizedWithCP56Time2a_create(
                nullptr, ioa, value, quality, &time56);
        case AsduType::M_ME_NB_1:
            return (InformationObject)MeasuredValueScaled_create(nullptr, ioa, integer, quality);
        case AsduType::M_ME_TB_1:
            return (InformationObject)MeasuredValueScaledWithCP24Time2a_create(
                nullptr, ioa, integer, quality, &time24);
        case AsduType::M_ME_TE_1:
            return (InformationObject)MeasuredValueScaledWithCP56Time2a_create(
                nullptr, ioa, integer, quality, &time56);
        case AsduType::M_ME_NC_1:
            return (InformationObject)MeasuredValueShort_create(nullptr, ioa, value, quality);
        case AsduType::M_ME_TC_1:
            return (InformationObject)MeasuredValueShortWithCP24Time2a_create(nullptr, ioa, value,
                                                                              quality, &time24);
        case AsduType::M_ME_TF_1:
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
        case ValueFamily::SinglePoint:
            sample.value = SinglePointInformation_getValue((SinglePointInformation)object) ? 1 : 0;
            sample.quality = SinglePointInformation_getQuality((SinglePointInformation)object);
            break;
        case ValueFamily::DoublePoint:
            sample.value = DoublePointInformation_getValue((DoublePointInformation)object);
            sample.quality = DoublePointInformation_getQuality((DoublePointInformation)object);
            break;
        case ValueFamily::Normalized:
            sample.value = MeasuredValueNormalized_getValue((MeasuredValueNormalized)object);
            sample.quality =
                type == enum_value(AsduType::M_ME_ND_1)
                    ? 0
                    : MeasuredValueNormalized_getQuality((MeasuredValueNormalized)object);
            break;
        case ValueFamily::Scaled:
            sample.value = MeasuredValueScaled_getValue((MeasuredValueScaled)object);
            sample.quality = MeasuredValueScaled_getQuality((MeasuredValueScaled)object);
            break;
        default:
            sample.value = MeasuredValueShort_getValue((MeasuredValueShort)object);
            sample.quality = MeasuredValueShort_getQuality((MeasuredValueShort)object);
            break;
    }
    switch (static_cast<AsduType>(type)) {
        case AsduType::M_SP_TB_1:
            timestamp = SinglePointWithCP56Time2a_getTimestamp((SinglePointWithCP56Time2a)object);
            break;
        case AsduType::M_DP_TB_1:
            timestamp = DoublePointWithCP56Time2a_getTimestamp((DoublePointWithCP56Time2a)object);
            break;
        case AsduType::M_ME_TD_1:
            timestamp = MeasuredValueNormalizedWithCP56Time2a_getTimestamp(
                (MeasuredValueNormalizedWithCP56Time2a)object);
            break;
        case AsduType::M_ME_TE_1:
            timestamp = MeasuredValueScaledWithCP56Time2a_getTimestamp(
                (MeasuredValueScaledWithCP56Time2a)object);
            break;
        case AsduType::M_ME_TF_1:
            timestamp = MeasuredValueShortWithCP56Time2a_getTimestamp(
                (MeasuredValueShortWithCP56Time2a)object);
            break;
        case AsduType::M_SP_TA_1:
            short_time = SinglePointWithCP24Time2a_getTimestamp((SinglePointWithCP24Time2a)object);
            break;
        case AsduType::M_DP_TA_1:
            short_time = DoublePointWithCP24Time2a_getTimestamp((DoublePointWithCP24Time2a)object);
            break;
        case AsduType::M_ME_TA_1:
            short_time = MeasuredValueNormalizedWithCP24Time2a_getTimestamp(
                (MeasuredValueNormalizedWithCP24Time2a)object);
            break;
        case AsduType::M_ME_TB_1:
            short_time = MeasuredValueScaledWithCP24Time2a_getTimestamp(
                (MeasuredValueScaledWithCP24Time2a)object);
            break;
        case AsduType::M_ME_TC_1:
            short_time = MeasuredValueShortWithCP24Time2a_getTimestamp(
                (MeasuredValueShortWithCP24Time2a)object);
            break;
    }
    if (timestamp) {
        sample.invalid_time = CP56Time2a_isInvalid(timestamp) || !valid_cp56(timestamp);
        if (!sample.invalid_time) {
            sample.timestamp =
                static_cast<std::int64_t>(CP56Time2a_toMsTimestamp(timestamp)) -
                time_offset * static_cast<long long>(enum_value(TimeUnits::MillisecondsPerMinute));
            sample.device_time = true;
        }
    }
    // CP24 缺少日期/小时，保留接收时刻，不猜测设备时间。
    if (short_time)
        sample.invalid_time =
            CP24Time2a_isInvalid(short_time) ||
            CP24Time2a_getMinute(short_time) >= enum_value(TimeUnits::SecondsPerMinute) ||
            CP24Time2a_getSecond(short_time) >= enum_value(TimeUnits::SecondsPerMinute) ||
            CP24Time2a_getMillisecond(short_time) >= enum_value(TimeUnits::MillisecondsPerSecond);
    return sample;
}

InformationObject command_object(const Address& address, double value, bool select, int qualifier,
                                 int time_offset) {
    sCP56Time2a timestamp{};
    CP56Time2a_createFromMsTimestamp(
        &timestamp, static_cast<std::uint64_t>(
                        now_ms() + time_offset * static_cast<long long>(enum_value(
                                                     TimeUnits::MillisecondsPerMinute))));
    const int ioa = address.ioa;
    const int integer = family(address.type) <= ValueFamily::Scaled ? static_cast<int>(value) : 0;
    const float floating = static_cast<float>(value);
    switch (static_cast<AsduType>(address.type)) {
        case AsduType::C_SC_NA_1:
            return (InformationObject)SingleCommand_create(nullptr, ioa, integer != 0, select,
                                                           qualifier);
        case AsduType::C_DC_NA_1:
            return (InformationObject)DoubleCommand_create(nullptr, ioa, integer, select,
                                                           qualifier);
        case AsduType::C_RC_NA_1:
            return (InformationObject)StepCommand_create(nullptr, ioa, (StepCommandValue)integer,
                                                         select, qualifier);
        case AsduType::C_SE_NA_1:
            return (InformationObject)SetpointCommandNormalized_create(nullptr, ioa, floating,
                                                                       select, qualifier);
        case AsduType::C_SE_NB_1:
            return (InformationObject)SetpointCommandScaled_create(nullptr, ioa, integer, select,
                                                                   qualifier);
        case AsduType::C_SE_NC_1:
            return (InformationObject)SetpointCommandShort_create(nullptr, ioa, floating, select,
                                                                  qualifier);
        case AsduType::C_SC_TA_1:
            return (InformationObject)SingleCommandWithCP56Time2a_create(
                nullptr, ioa, integer != 0, select, qualifier, &timestamp);
        case AsduType::C_DC_TA_1:
            return (InformationObject)DoubleCommandWithCP56Time2a_create(
                nullptr, ioa, integer, select, qualifier, &timestamp);
        case AsduType::C_RC_TA_1:
            return (InformationObject)StepCommandWithCP56Time2a_create(
                nullptr, ioa, (StepCommandValue)integer, select, qualifier, &timestamp);
        case AsduType::C_SE_TA_1:
            return (InformationObject)SetpointCommandNormalizedWithCP56Time2a_create(
                nullptr, ioa, floating, select, qualifier, &timestamp);
        case AsduType::C_SE_TB_1:
            return (InformationObject)SetpointCommandScaledWithCP56Time2a_create(
                nullptr, ioa, integer, select, qualifier, &timestamp);
        case AsduType::C_SE_TC_1:
            return (InformationObject)SetpointCommandShortWithCP56Time2a_create(
                nullptr, ioa, floating, select, qualifier, &timestamp);
        default:
            return nullptr;
    }
}

std::tuple<double, bool, int> command_value(int type, InformationObject object) {
    switch (family(type)) {
        case ValueFamily::SinglePoint:
            return {SingleCommand_getState((SingleCommand)object) ? 1.0 : 0.0,
                    SingleCommand_isSelect((SingleCommand)object),
                    SingleCommand_getQU((SingleCommand)object)};
        case ValueFamily::DoublePoint:
            return {DoubleCommand_getState((DoubleCommand)object),
                    DoubleCommand_isSelect((DoubleCommand)object),
                    DoubleCommand_getQU((DoubleCommand)object)};
        case ValueFamily::Step:
            return {StepCommand_getState((StepCommand)object),
                    StepCommand_isSelect((StepCommand)object),
                    StepCommand_getQU((StepCommand)object)};
        case ValueFamily::Normalized:
            return {SetpointCommandNormalized_getValue((SetpointCommandNormalized)object),
                    SetpointCommandNormalized_isSelect((SetpointCommandNormalized)object),
                    SetpointCommandNormalized_getQL((SetpointCommandNormalized)object)};
        case ValueFamily::Scaled:
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
    std::shared_ptr<TrafficObserver> traffic{std::make_shared<TrafficObserver>()};
    mutable std::mutex mutex;
    std::condition_variable cv;
    CS104_Connection connection{nullptr};
    CS104_Slave server{nullptr};
    bool configured{false}, server_role{false}, ready{false}, opened{false}, closed{true};
    bool initial_gi{true}, select_execute{false}, require_termination{false};
    std::string host, error;
    int port{enum_value(Defaults::Port)}, timeout_ms{enum_value(Defaults::TimeoutMs)},
        command_timeout_ms{enum_value(Defaults::CommandTimeoutMs)},
        stale_ms{enum_value(Defaults::StaleMs)};
    int originator{0}, max_points{enum_value(Defaults::MaxPoints)},
        max_sessions{enum_value(Defaults::MaxSessions)},
        queue_size{enum_value(Defaults::QueueSize)}, qualifier{0};
    int time_offset{0};
    sCS104_APCIParameters apci{enum_value(Defaults::SendWindow),
                               enum_value(Defaults::ReceiveWindow),
                               enum_value(Defaults::ConnectionTimeoutSeconds),
                               enum_value(Defaults::MessageTimeoutSeconds),
                               enum_value(Defaults::ConfirmIntervalSeconds),
                               enum_value(Defaults::KeepAliveSeconds)};
    std::set<int> stations;
    std::map<std::tuple<int, int, int>, Sample> samples;

    struct Pending {
        bool active{false}, confirmed{false}, done{false}, rejected{false};
        Address address{0, 0, 0};
        std::tuple<double, bool, int> value{0, false, 0};
        std::string rejection;
    } pending;

    /** @brief 北向控制点：CA:IOA:ASDU 到本地稳定点 ID；不进入监视映像。 */
    std::map<std::tuple<int, int, int>, std::string> control_points;

    struct Selection {
        std::tuple<double, bool, int> value{0, false, 0};
        Clock::time_point expires{Clock::now()};
    };

    /** @brief 每个会话每个控制点至多一个待执行选择。 */
    std::map<std::pair<const void*, std::tuple<int, int, int>>, Selection> selections;

    struct Inbound {
        std::int64_t id{0};
        IMasterConnection peer{nullptr};
        AsduPtr request{nullptr, CS101_ASDU_destroy};
        std::string code, point_id;
        double value{0};
        std::int64_t expires_at_ms{0};
        bool delivered{false};
    };

    /** @brief 已受理、等待宿主执行并回执的北向控制。 */
    std::map<std::int64_t, Inbound> inbound;
    std::int64_t next_control_id{1};
    int select_timeout_ms{30000}, control_queue_size{64};
    bool require_select{true};

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
                    InformationObject_getObjectAddress(object.get()) != self->pending.address.ioa)
                    return true;
                const char* rejection = nullptr;
                switch (cot) {
                    case CS101_COT_UNKNOWN_TYPE_ID:
                        rejection = "unsupported_asdu_type";
                        break;
                    case CS101_COT_UNKNOWN_COT:
                        rejection = "unsupported_cause_of_transmission";
                        break;
                    case CS101_COT_UNKNOWN_CA:
                        rejection = "unknown_common_address";
                        break;
                    case CS101_COT_UNKNOWN_IOA:
                        rejection = "unknown_information_object_address";
                        break;
                    default:
                        break;
                }
                if (rejection) {
                    self->pending.rejected = self->pending.done = true;
                    self->pending.rejection = rejection;
                    self->cv.notify_all();
                    return true;
                }
                if (command_value(type, object.get()) != self->pending.value) return true;
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

    /** @brief 向上级回执结束确认；连接已关闭时发送会安全失败。 */
    static void finish(IMasterConnection peer, CS101_ASDU asdu, bool accepted) {
        if (!peer || !asdu) return;
        CS101_ASDU_setCOT(asdu, CS101_COT_ACTIVATION_TERMINATION);
        CS101_ASDU_setNegative(asdu, !accepted);
        IMasterConnection_sendASDU(peer, asdu);
    }

    /** @brief 会话关闭时丢弃该会话的选择与待回执请求，避免回执发往已复用的连接对象。 */
    static void server_connection(void* parameter, IMasterConnection peer,
                                  CS104_PeerConnectionEvent event) noexcept {
        if (event != CS104_CON_EVENT_CONNECTION_CLOSED) return;
        auto* self = static_cast<Impl*>(parameter);
        try {
            std::lock_guard<std::mutex> lock(self->mutex);
            for (auto it = self->selections.begin(); it != self->selections.end();)
                it = it->first.first == peer ? self->selections.erase(it) : std::next(it);
            for (auto it = self->inbound.begin(); it != self->inbound.end();)
                it = it->second.peer == peer ? self->inbound.erase(it) : std::next(it);
        } catch (...) {
        }
    }

    /** @brief 受理上级选择/执行：选择只登记窗口，执行才排入本地命令队列。 */
    static bool server_control(void* parameter, IMasterConnection peer,
                               CS101_ASDU request) noexcept {
        auto* self = static_cast<Impl*>(parameter);
        try {
            const int type = CS101_ASDU_getTypeID(request);
            if (!control(type)) return false;  // 非控制类型交给协议栈回负应答。
            auto reply = [&](CS101_CauseOfTransmission cause, bool negative) {
                CS101_ASDU_setCOT(request, cause);
                CS101_ASDU_setNegative(request, negative);
                return IMasterConnection_sendASDU(peer, request);
            };
            const int ca = CS101_ASDU_getCA(request);
            if (CS101_ASDU_isTest(request) || CS101_ASDU_getCOT(request) != CS101_COT_ACTIVATION)
                return reply(CS101_COT_UNKNOWN_COT, true);
            if (!self->stations.count(ca)) return reply(CS101_COT_UNKNOWN_CA, true);
            if (CS101_ASDU_getNumberOfElements(request) != 1)
                return reply(CS101_COT_UNKNOWN_IOA, true);
            ObjectPtr object(CS101_ASDU_getElement(request, 0), InformationObject_destroy);
            if (!object) return reply(CS101_COT_UNKNOWN_IOA, true);
            const int ioa = InformationObject_getObjectAddress(object.get());
            const auto value = command_value(type, object.get());
            try {
                validate_value(type, std::get<0>(value));
            } catch (...) {
                return reply(CS101_COT_ACTIVATION_CON, true);
            }
            bool negative = false;
            CS101_CauseOfTransmission cause = CS101_COT_ACTIVATION_CON;
            {
                std::lock_guard<std::mutex> lock(self->mutex);
                const auto key = std::make_tuple(ca, ioa, type);
                const auto point = self->control_points.find(key);
                if (point == self->control_points.end()) {
                    negative = true;
                    cause = CS101_COT_UNKNOWN_IOA;
                } else if (std::get<1>(value)) {
                    self->selections[{peer, key}] = {
                        value, Clock::now() + std::chrono::milliseconds(self->select_timeout_ms)};
                } else {
                    const auto selection = self->selections.find({peer, key});
                    // 选择与执行只比较编码后的值与限定词，选择位本身必然不同。
                    const bool selected =
                        selection != self->selections.end() &&
                        std::get<0>(selection->second.value) == std::get<0>(value) &&
                        std::get<2>(selection->second.value) == std::get<2>(value) &&
                        Clock::now() < selection->second.expires;
                    self->selections.erase({peer, key});
                    if (self->require_select && !selected) {
                        negative = true;
                    } else if (self->inbound.size() >=
                               static_cast<std::size_t>(self->control_queue_size)) {
                        negative = true;
                    } else {
                        Inbound item;
                        item.id = self->next_control_id++;
                        item.peer = peer;
                        item.request.reset(CS101_ASDU_clone(request, nullptr));
                        item.code = std::to_string(ca) + ":" + std::to_string(ioa) + ":" +
                                    std::to_string(type);
                        item.point_id = point->second;
                        item.value = std::get<0>(value);
                        item.expires_at_ms = now_ms() + self->command_timeout_ms;
                        self->inbound.emplace(item.id, std::move(item));
                    }
                }
            }
            return reply(cause, negative);
        } catch (...) {
            return false;
        }
    }
};

Iec104Protocol::Iec104Protocol() : impl_(std::make_unique<Impl>()) {}

Iec104Protocol::~Iec104Protocol() { impl_->stop(); }

void Iec104Protocol::set_traffic_handler(ems_plugin_traffic_callback callback, void* user) {
    impl_->traffic->set(callback, user);
}

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
        const int time_offset = option_int(config, "cp56_timezone_offset_minutes", 0,
                                           enum_value(Limits::MinTimezoneMinutes),
                                           enum_value(Limits::MaxTimezoneMinutes));
        const auto role = config.value("local_role", config.value("role", std::string("client")));
        if (role != "client" && role != "server" && role != "server_readonly")
            throw std::invalid_argument("Invalid local_role");
        const bool server_role = role != "client";
        std::string host = config.value(
            "host", config.value("ip", std::string(server_role ? "0.0.0.0" : "127.0.0.1")));
        int port =
            option_int(config, "port", enum_value(Defaults::Port), 1, enum_value(Limits::MaxPort));
        if (config.contains("connection") && !config.contains("host") && !config.contains("ip")) {
            const auto endpoint = config.at("connection").get<std::string>();
            const auto colon = endpoint.rfind(':');
            if (colon == std::string::npos)
                host = endpoint;
            else {
                host = endpoint.substr(0, colon);
                if (!config.contains("port"))
                    port = number(endpoint.substr(colon + 1), 1, enum_value(Limits::MaxPort));
            }
        }
        if (host.empty() || host.size() > enum_value(Limits::MaxHostBytes) ||
            host.find('\0') != std::string::npos || host.find(':') != std::string::npos)
            throw std::invalid_argument("Endpoint must be IPv4 or hostname supported by lib60870");
        std::set<int> stations;
        auto addresses = config.value(
            "common_addresses",
            config.value("slaves",
                         Json::array({option_int(config, "common_address", 1, 1,
                                                 enum_value(Limits::MaxCommonAddress))})));
        if (!addresses.is_array() || addresses.empty() ||
            addresses.size() > enum_value(Limits::MaxStations))
            throw std::invalid_argument("common_addresses requires 1..256 stations");
        for (const auto& ca : addresses) {
            if (!ca.is_number_integer())
                throw std::invalid_argument("Common address must be integer");
            const auto value = ca.get<std::int64_t>();
            if (value < 1 || value > enum_value(Limits::MaxCommonAddress) ||
                !stations.insert(static_cast<int>(value)).second)
                throw std::invalid_argument("Invalid/duplicate common address");
        }
        const int max_points = option_int(config, "max_points", enum_value(Defaults::MaxPoints), 1,
                                          enum_value(Limits::MaxPoints));
        std::map<std::tuple<int, int, int>, Sample> samples;
        std::map<std::tuple<int, int, int>, std::string> control_points;
        const auto points = config.value("points", Json::array());
        if (!points.is_array() || points.size() > static_cast<std::size_t>(max_points))
            throw std::invalid_argument("Invalid point image size");
        for (const auto& point : points) {
            const auto address = parse(point.at("code").get<std::string>());
            if (!stations.count(address.ca))
                throw std::invalid_argument("Point must be in configured station");
            if (control(address.type)) {
                // 北向控制点只登记本地路由，不进入监视映像，也不参与总召。
                if (!server_role) throw std::invalid_argument("Control points require server role");
                const auto point_id = point.value("point_id", std::string());
                if (point_id.empty() || point_id.size() > 64)
                    throw std::invalid_argument("Control point requires 1..64 byte point_id");
                if (!control_points.emplace(address.key(), point_id).second)
                    throw std::invalid_argument("Duplicate control point");
                continue;
            }
            Sample sample;
            sample.value = point.value("value", 0.0);
            validate_value(address.type, sample.value);
            sample.quality = option_int(point, "quality", enum_value(SampleQuality::Good), 0,
                                        enum_value(Limits::MaxQualityByte));
            if ((sample.quality & ~enum_value(QualityMask::Supported)) ||
                (family(address.type) <= ValueFamily::DoublePoint &&
                 (sample.quality & enum_value(QualityMask::Overflow))) ||
                (address.type == enum_value(AsduType::M_ME_ND_1) && sample.quality))
                throw std::invalid_argument("Invalid IEC quality bits for ASDU");
            if (point.contains("timestamp") && !point.at("timestamp").is_number_integer())
                throw std::invalid_argument("Point timestamp must be integer milliseconds");
            sample.timestamp = point.value("timestamp", now_ms());
            if (sample.timestamp < enum_value(TimeEncoding::Epoch2000Ms) -
                                       time_offset * static_cast<long long>(enum_value(
                                                         TimeUnits::MillisecondsPerMinute)) ||
                sample.timestamp >= enum_value(TimeEncoding::Epoch2100Ms) -
                                        time_offset * static_cast<long long>(enum_value(
                                                          TimeUnits::MillisecondsPerMinute)))
                throw std::invalid_argument("Point timestamp must encode within years 2000..2099");
            if (!samples.emplace(address.key(), sample).second)
                throw std::invalid_argument("Duplicate image point");
        }
        sCS104_APCIParameters apci{};
        apci.k = option_int(config, "send_window_size", enum_value(Defaults::SendWindow), 1,
                            enum_value(Limits::MaxWindow));
        apci.w = option_int(config, "receive_window_size", enum_value(Defaults::ReceiveWindow), 1,
                            apci.k);
        apci.t0 =
            option_int(config, "connection_timeout", enum_value(Defaults::ConnectionTimeoutSeconds),
                       1, enum_value(Limits::MaxConnectionTimeoutSeconds));
        apci.t1 = option_int(config, "message_timeout", enum_value(Defaults::MessageTimeoutSeconds),
                             1, enum_value(Limits::MaxMessageTimeoutSeconds));
        apci.t2 =
            option_int(config, "confirm_interval", enum_value(Defaults::ConfirmIntervalSeconds), 1,
                       enum_value(Limits::MaxMessageTimeoutSeconds));
        apci.t3 = option_int(config, "keep_alive_interval", enum_value(Defaults::KeepAliveSeconds),
                             1, enum_value(Limits::MaxKeepAliveSeconds));
        if (apci.t2 >= apci.t1)
            throw std::invalid_argument("confirm_interval must be less than message_timeout");
        const int timeout = option_int(config, "timeout_ms", enum_value(Defaults::TimeoutMs), 1,
                                       enum_value(Limits::MaxTimeoutMs));
        const int command_timeout =
            option_int(config, "command_timeout_ms", enum_value(Defaults::CommandTimeoutMs), 1,
                       enum_value(Limits::MaxTimeoutMs));
        const int stale = option_int(config, "stale_ms", enum_value(Defaults::StaleMs), 0,
                                     enum_value(Limits::MaxStaleMs));
        const int oa =
            option_int(config, "originator_address", 0, 0, enum_value(Limits::MaxOriginator));
        const int sessions =
            option_int(config, "max_connections", enum_value(Defaults::MaxSessions), 1,
                       enum_value(Limits::MaxSessions));
        const int queue =
            option_int(config, "queue_size", enum_value(Defaults::QueueSize),
                       enum_value(Limits::MinQueueSize), enum_value(Limits::MaxQueueSize));
        const int qualifier =
            option_int(config, "command_qualifier", 0, 0, enum_value(Limits::MaxQualifier));
        const bool initial_gi = config.value("general_interrogation_on_connect", true);
        const bool select_execute = config.value("select_execute", false);
        // ACTCON 是协议受理确认；实际状态由采集侧反馈核验，ACTTERM 只在显式配置时等待。
        const bool termination = config.value("command_require_termination", false);
        // 从站选择窗口与受理队列；require_select 为真时未选择直接执行回负确认。
        const int select_timeout = option_int(config, "select_timeout_ms", 30000, 1000, 600000);
        const int control_queue = option_int(config, "control_queue_size", 64, 1, 1024);
        const bool require_select = config.value("require_select", true);
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->server_role = server_role;
        impl_->host = std::move(host);
        impl_->port = port;
        impl_->stations = std::move(stations);
        impl_->samples = server_role ? std::move(samples) : decltype(samples){};
        impl_->control_points =
            server_role ? std::move(control_points) : decltype(control_points){};
        impl_->selections.clear();
        impl_->inbound.clear();
        impl_->next_control_id = 1;
        impl_->select_timeout_ms = select_timeout;
        impl_->control_queue_size = control_queue;
        impl_->require_select = require_select;
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
        return enum_value(OperationResult::Success);
    } catch (const std::exception& error) {
        impl_->set_error(error.what());
        return enum_value(OperationResult::Failure);
    }
}

int Iec104Protocol::connect() {
    if (is_connected()) return enum_value(OperationResult::Success);
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
            CS104_Slave_setASDUHandler(impl_->server, Impl::server_control, impl_.get());
            CS104_Slave_setConnectionEventHandler(impl_->server, Impl::server_connection,
                                                  impl_.get());
            CS104_Slave_setRawMessageHandler(
                impl_->server,
                [](void* user, IMasterConnection connection, uint8_t* bytes, int size, bool sent) {
                    static_cast<TrafficObserver*>(user)->emit(
                        sent ? EMS_TRAFFIC_TX : EMS_TRAFFIC_RX, bytes, size,
                        reinterpret_cast<uintptr_t>(connection));
                },
                impl_->traffic.get());
            CS104_Slave_start(impl_->server);
            if (!CS104_Slave_isRunning(impl_->server))
                throw std::runtime_error("IEC104 listen failed");
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->ready = true;
            impl_->closed = false;
            impl_->error.clear();
            return enum_value(OperationResult::Success);
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
        CS104_Connection_setRawMessageHandler(
            impl_->connection,
            [](void* user, uint8_t* bytes, int size, bool sent) {
                static_cast<TrafficObserver*>(user)->emit(sent ? EMS_TRAFFIC_TX : EMS_TRAFFIC_RX,
                                                          bytes, size);
            },
            impl_->traffic.get());
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
        return enum_value(OperationResult::Success);
    } catch (const std::exception& error) {
        impl_->stop();
        impl_->set_error(error.what());
        return enum_value(OperationResult::Failure);
    }
}

int Iec104Protocol::disconnect() {
    impl_->stop();
    return enum_value(OperationResult::Success);
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
        return enum_value(OperationResult::Failure);
    }
    const auto deadline = Clock::now() + std::chrono::milliseconds(impl_->timeout_ms);
    for (const auto station : impl_->stations) {
        if (ca && station != ca) continue;
        while (CS104_Connection_isTransmitBufferFull(impl_->connection) && is_connected() &&
               Clock::now() < deadline) {
            std::unique_lock<std::mutex> lock(impl_->mutex);
            impl_->cv.wait_for(lock,
                               std::chrono::milliseconds(enum_value(Defaults::ConnectPollMs)));
        }
        if (!is_connected() ||
            !CS104_Connection_sendInterrogationCommand(impl_->connection, CS101_COT_ACTIVATION,
                                                       station, IEC60870_QOI_STATION)) {
            impl_->set_error("IEC104 send window full or interrogation send failed");
            return enum_value(OperationResult::Failure);
        }
    }
    return enum_value(OperationResult::Success);
}

int Iec104Protocol::read_batch(const std::vector<std::string>& codes, std::string& result) {
    if (!is_connected()) {
        impl_->set_error("IEC104 is not ready");
        return enum_value(OperationResult::Failure);
    }
    if (codes.size() > enum_value(PluginLimits::MaxBatchPoints)) {
        impl_->set_error("Batch too large");
        return enum_value(OperationResult::Failure);
    }
    Json rows = Json::array();
    int count = 0;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (const auto& code : codes) {
        Json row = {{"code", code},
                    {"value", nullptr},
                    {"quality", enum_value(SampleQuality::Invalid)},
                    {"timestamp", now_ms()}};
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
            const bool valid = !stale && !sample.quality && !sample.invalid_time &&
                               std::isfinite(sample.value) &&
                               (family(address.type) != ValueFamily::DoublePoint ||
                                sample.value == enum_value(DoublePointState::Off) ||
                                sample.value == enum_value(DoublePointState::On));
            row["timestamp"] = sample.timestamp;
            row["received_at_ms"] = sample.received_at_ms;
            row["iec_quality"] = sample.quality;
            row["timestamp_source"] = impl_->server_role   ? "local_image"
                                      : sample.device_time ? "device_cp56"
                                                           : "received";
            row["stale"] = stale;
            if (valid) {
                row["value"] = sample.value;
                row["quality"] = enum_value(SampleQuality::Good);
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
    return write_until(code, value, now_ms() + impl_->command_timeout_ms);
}

int Iec104Protocol::write_until(const std::string& code, double value, std::int64_t expires_at_ms) {
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
                sample.received_at_ms = now_ms();
                found->second = sample;
            }
            auto asdu = impl_->make_asdu(address, sample, CS101_COT_SPONTANEOUS, impl_->originator);
            CS104_Slave_enqueueASDU(impl_->server, asdu.get());
            return enum_value(OperationResult::Success);
        }
        if (!control(address.type))
            throw std::invalid_argument("Client write requires command ASDU");
        const auto wall = now_ms();
        // 调用方期限与通道命令期限取较小值；已过期则不发帧，也不破坏现有会话。
        if (expires_at_ms <= wall)
            throw std::runtime_error("Command deadline expired before send; not sent");
        const auto budget = std::min<std::int64_t>(expires_at_ms - wall, impl_->command_timeout_ms);
        const auto deadline = Clock::now() + std::chrono::milliseconds(budget);
        for (int phase = impl_->select_execute ? 0 : 1; phase <= 1; ++phase) {
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
            const bool confirmed = impl_->pending.confirmed;
            impl_->pending.active = false;
            if (!done) {
                lock.unlock();
                impl_->stop();
                if (confirmed)
                    throw std::runtime_error(
                        "IEC104 activation confirmed; termination timeout/disconnection; outcome "
                        "unknown, session closed; do not auto retry");
                throw std::runtime_error(
                    "Command timeout/disconnection; outcome unknown, session closed; do not auto "
                    "retry");
            }
            if (rejected) {
                if (!impl_->pending.rejection.empty())
                    throw std::runtime_error("IEC104 command rejected: " +
                                             impl_->pending.rejection);
                throw std::runtime_error("IEC104 command negatively confirmed");
            }
        }
        return enum_value(OperationResult::Success);
    } catch (const std::exception& error) {
        impl_->set_error(error.what());
        return enum_value(OperationResult::Failure);
    }
}

int Iec104Protocol::take_controls(std::string& result) {
    // 客户端实例不支持北向控制；不写错误，避免每次轮询覆盖真实诊断。
    if (!impl_->server_role) return enum_value(OperationResult::Failure);
    Json rows = Json::array();
    std::vector<Impl::Inbound> expired;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto now = now_ms();
        for (auto it = impl_->inbound.begin(); it != impl_->inbound.end();) {
            if (it->second.expires_at_ms > now) {
                ++it;
                continue;
            }
            expired.push_back(std::move(it->second));
            it = impl_->inbound.erase(it);
        }
        for (auto& item : impl_->inbound) {
            if (item.second.delivered) continue;
            item.second.delivered = true;
            rows.push_back({{"id", item.second.id},
                            {"code", item.second.code},
                            {"point_id", item.second.point_id},
                            {"value", item.second.value},
                            {"expires_at_ms", item.second.expires_at_ms}});
        }
    }
    // 已过期但未回执的请求回负结束确认，避免上级无限等待。
    for (auto& item : expired) Impl::finish(item.peer, item.request.get(), false);
    result = rows.dump();
    return static_cast<int>(rows.size());
}

int Iec104Protocol::resolve_control(std::int64_t control_id, bool accepted,
                                    const std::string& reason) {
    if (!impl_->server_role) {
        impl_->set_error("Northbound controls require server role");
        return enum_value(OperationResult::Failure);
    }
    Impl::Inbound item;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto found = impl_->inbound.find(control_id);
        if (found == impl_->inbound.end()) {
            impl_->error = "Unknown or already resolved control id";
            return enum_value(OperationResult::Failure);
        }
        item = std::move(found->second);
        impl_->inbound.erase(found);
    }
    // 已停止监听时会话随栈一起销毁，不需要补发结束确认。
    if (!impl_->server) return enum_value(OperationResult::Success);
    Impl::finish(item.peer, item.request.get(), accepted);
    if (!accepted) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->error = "IEC104 control rejected: " +
                       (reason.empty() ? std::string("execution_failed") : reason);
    }
    return enum_value(OperationResult::Success);
}

}  // namespace ems::net::protocol
