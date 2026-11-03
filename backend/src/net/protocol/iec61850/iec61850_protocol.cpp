#include "net/protocol/iec61850_protocol.h"

#include <iec61850_client.h>
#include <mms_type_spec.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

#include "mms_server_runtime.h"
#include "net/protocol/protocol_types.h"

namespace ems::net::protocol {
namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using Spec = std::unique_ptr<MmsVariableSpecification, decltype(&MmsVariableSpecification_destroy)>;
using Value = std::unique_ptr<MmsValue, decltype(&MmsValue_delete)>;
using Data = std::unique_ptr<sClientDataSet, decltype(&ClientDataSet_destroy)>;

std::uint64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void require(bool condition, const std::string& message) {
    if (!condition) throw std::invalid_argument(message);
}

void fields(const Json& value, const std::set<std::string>& allowed) {
    require(value.is_object(), "configuration must be an object");
    for (auto it = value.begin(); it != value.end(); ++it)
        require(allowed.count(it.key()) != 0, "unsupported configuration field: " + it.key());
}

int integer(const Json& value, const char* key, int fallback, int minimum, int maximum) {
    if (!value.contains(key)) return fallback;
    const auto& item = value.at(key);
    require(item.is_number_integer(), std::string(key) + " must be an integer");
    const auto number = item.get<std::int64_t>();
    require(number >= minimum && number <= maximum, std::string(key) + " out of range");
    return static_cast<int>(number);
}

std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> result;
    std::size_t start = 0;
    do {
        const auto end = text.find(separator, start);
        result.push_back(text.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    } while (true);
    return result;
}

bool identifier(const std::string& text) {
    return !text.empty() &&
           text.find_first_not_of(
               "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") ==
               std::string::npos;
}

struct Step {
    std::string name;
    int index{-1};
};

struct Reference {
    std::string base;
    std::vector<Step> steps;
};

Reference reference(const std::string& text) {
    const auto slash = text.find('/');
    require(slash != std::string::npos && slash <= 64 && identifier(text.substr(0, slash)),
            "invalid LD reference: " + text);
    require(text.size() - slash - 1 + 3 <= 64, "MMS item reference exceeds 64 bytes: " + text);
    const auto tokens = split(text.substr(slash + 1), '.');
    require(tokens.size() >= 2 && identifier(tokens.front()), "invalid object reference: " + text);
    Reference parsed;
    int arrays = 0;
    for (std::size_t i = 1; i < tokens.size(); ++i) {
        Step step{tokens[i], -1};
        const auto bracket = step.name.find('(');
        if (bracket != std::string::npos) {
            const auto digits = step.name.substr(bracket + 1, step.name.size() - bracket - 2);
            require(step.name.back() == ')' && !digits.empty() && digits.size() <= 6 &&
                        digits.find_first_not_of("0123456789") == std::string::npos &&
                        ++arrays == 1,
                    "invalid or nested array index: " + text);
            step.index = std::stoi(digits);
            step.name.resize(bracket);
        }
        require(identifier(step.name), "invalid object reference: " + text);
        parsed.steps.push_back(std::move(step));
    }
    parsed.base = text.substr(0, slash + 1) + tokens[0] + "." + parsed.steps[0].name;
    return parsed;
}

FunctionalConstraint constraint(const std::string& name) {
    require(std::set<std::string>{"MX", "ST", "SP", "CF", "DC", "EX"}.count(name) != 0,
            "unsupported read FC: " + name);
    return FunctionalConstraint_fromString(name.c_str());
}

std::string dataset_ref(std::string text) {
    const auto slash = text.find('/');
    require(slash != std::string::npos && slash <= 64 && identifier(text.substr(0, slash)),
            "invalid DataSet reference: " + text);
    auto local = text.substr(slash + 1);
    std::replace(local.begin(), local.end(), '.', '$');
    const auto tokens = split(local, '$');
    require(
        local.size() <= 64 && tokens.size() == 2 && identifier(tokens[0]) && identifier(tokens[1]),
        "invalid persistent DataSet reference: " + text);
    return text.substr(0, slash + 1) + local;
}

std::string key(const std::string& ref, FunctionalConstraint fc) {
    return std::string(FunctionalConstraint_toString(fc)) + ":" + ref;
}

struct Point {
    std::string code;
    std::string ref;
    FunctionalConstraint fc{IEC61850_FC_NONE};
    std::string type;
    std::string quality;
    std::string timestamp;
    std::set<double> invalid;
};

struct ReportConfig {
    std::string ref, dataset;
    bool required{true}, gi{true};
    int integrity{10000};
    int triggers{TRG_OPT_DATA_CHANGED | TRG_OPT_QUALITY_CHANGED | TRG_OPT_DATA_UPDATE |
                 TRG_OPT_INTEGRITY | TRG_OPT_GI};
};

struct Sample {
    MmsValue* value{nullptr};
    std::string source, group, error;
    std::uint64_t received{0};
    Clock::time_point arrival{};
    std::shared_ptr<MmsValue> owned;
    Json report;
};

struct Config {
    std::string host;
    bool server{false};
    std::string server_options;
    std::string read_mode{"poll"};
    int poll_interval{1000}, queue_size{1024};
    std::vector<ReportConfig> reports;
    int port{102};
    int timeout{3000};
    int connect_timeout{3000};
    int discovery_timeout{60000};
    int stale{30000};
    int max_points{65536};
    int max_nodes{100000};
    int max_depth{32};
    std::vector<Point> points;
    std::vector<std::string> datasets;
    std::set<std::string> required_datasets;
};

Config configuration(const std::string& text) {
    auto value = Json::parse(text);
    require(value.is_object(), "config must be a JSON object");
    if (value.contains("settings")) {
        auto settings = value.at("settings");
        if (settings.is_string()) settings = Json::parse(settings.get<std::string>());
        require(settings.is_object(), "settings must be an object");
        for (auto it = settings.begin(); it != settings.end(); ++it)
            if (!value.contains(it.key())) value[it.key()] = it.value();
    }
    fields(value, {"settings", "protocol", "local_role", "role", "transport", "connection",
                   "timeout_ms", "poll_interval_ms", "retry_times", "max_sessions", "channel_id",
                   "name", "baud_rate", "data_bits", "stop_bits", "parity", "iec61850"});
    require(value.value("protocol", std::string("IEC61850")) == "IEC61850",
            "protocol must be IEC61850");
    const auto role = value.value("local_role", value.value("role", std::string("client")));
    const auto role_is_server = [](const std::string& name) {
        return name == "server" || name == "server_readonly";
    };
    require(role == "client" || role_is_server(role), "invalid MMS role");
    const auto other_role = value.value("role", role);
    require((role == "client" && other_role == "client") ||
                (role_is_server(role) && role_is_server(other_role)),
            "conflicting MMS roles");
    require(value.value("transport", std::string("tcp")) == "tcp", "MMS requires TCP");
    Config config;
    config.server = role_is_server(role);
    const auto endpoint = value.at("connection").get<std::string>();
    const auto colon = endpoint.rfind(':');
    require(colon != std::string::npos && colon > 0 && colon + 1 < endpoint.size(),
            "connection must be host:port");
    config.host = endpoint.substr(0, colon);
    const auto port = endpoint.substr(colon + 1);
    require(config.host.size() <= 253 &&
                config.host.find_first_not_of(
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-") ==
                    std::string::npos,
            "only IPv4 addresses or DNS hostnames are supported");
    require(port.size() <= 5 && port.find_first_not_of("0123456789") == std::string::npos,
            "invalid port");
    config.port = std::stoi(port);
    require(config.port >= 1 && config.port <= 65535, "port out of range");
    config.timeout = integer(value, "timeout_ms", 3000, 1, 60000);
    const auto& options = value.at("iec61850");
    require(options.is_object(), "iec61850 must be an object");
    if (config.server) {
        config.server_options = options.dump();
        return config;
    }
    fields(options, {"connect_timeout_ms", "discovery_timeout_ms", "read_mode", "stale_ms",
                     "allow_writes", "max_points", "max_model_nodes", "max_model_depth",
                     "report_queue_size", "datasets", "reports", "points"});
    config.read_mode = options.value("read_mode", std::string("poll"));
    require(
        config.read_mode == "poll" || config.read_mode == "report" || config.read_mode == "hybrid",
        "invalid read_mode");
    require(!options.value("allow_writes", false), "client writes are not supported");
    config.poll_interval = integer(value, "poll_interval_ms", 1000, 1, 86400000);
    config.connect_timeout = integer(options, "connect_timeout_ms", 3000, 1, 60000);
    config.discovery_timeout = integer(options, "discovery_timeout_ms", 60000, 1, 600000);
    config.stale = integer(options, "stale_ms", 30000, 0, 86400000);
    config.max_points = integer(options, "max_points", 65536, 1, 65536);
    config.max_nodes = integer(options, "max_model_nodes", 100000, 1, 1000000);
    config.max_depth = integer(options, "max_model_depth", 32, 1, 64);
    config.queue_size = integer(options, "report_queue_size", 1024, 16, 65536);
    const auto& points = options.at("points");
    require(points.is_array() && points.size() <= static_cast<std::size_t>(config.max_points),
            "points must be an array within max_points");
    std::set<std::string> codes;
    for (const auto& item : points) {
        fields(item, {"code", "object_ref", "fc", "mms_type", "quality_ref", "timestamp_ref",
                      "invalid_values"});
        Point point;
        point.code = item.at("code").get<std::string>();
        require(!point.code.empty() && point.code.size() <= 256 &&
                    point.code.find('\0') == std::string::npos && codes.insert(point.code).second,
                "point code is invalid or duplicated");
        point.ref = item.at("object_ref").get<std::string>();
        reference(point.ref);
        point.fc = constraint(item.at("fc").get<std::string>());
        point.type = item.value("mms_type", std::string());
        require(
            std::set<std::string>{"", "boolean", "integer", "unsigned", "float32", "float64"}.count(
                point.type) != 0,
            "unsupported mms_type: " + point.type);
        point.quality = item.value("quality_ref", std::string());
        point.timestamp = item.value("timestamp_ref", std::string());
        if (!point.quality.empty()) reference(point.quality);
        if (!point.timestamp.empty()) reference(point.timestamp);
        if (item.contains("invalid_values")) {
            require(item.at("invalid_values").is_array() && item.at("invalid_values").size() <= 256,
                    "invalid_values must be a bounded array");
            for (const auto& invalid : item.at("invalid_values")) {
                require(invalid.is_number() && std::isfinite(invalid.get<double>()),
                        "invalid_values must contain finite numbers");
                point.invalid.insert(invalid.get<double>());
            }
        }
        config.points.push_back(std::move(point));
    }
    if (options.contains("datasets")) {
        require(options.at("datasets").is_array() && options.at("datasets").size() <= 4096,
                "datasets must be a bounded array");
        std::set<std::string> refs;
        for (const auto& item : options.at("datasets")) {
            auto ref = dataset_ref(item.get<std::string>());
            require(refs.insert(ref).second, "duplicate DataSet reference");
            config.datasets.push_back(std::move(ref));
            config.required_datasets.insert(config.datasets.back());
        }
    }
    if (options.contains("reports")) {
        const auto& reports = options.at("reports");
        require(reports.is_array() && reports.size() <= 128, "invalid reports list");
        std::set<std::string> refs;
        for (const auto& item : reports) {
            fields(item, {"rcb_ref", "dataset_ref", "required", "gi_on_connect",
                          "integrity_period_ms", "trigger_options"});
            ReportConfig report;
            report.ref = item.at("rcb_ref").get<std::string>();
            const auto slash = report.ref.find('/');
            require(slash != std::string::npos && slash <= 64 &&
                        identifier(report.ref.substr(0, slash)),
                    "invalid RCB LD");
            const auto tokens = split(report.ref.substr(slash + 1), '.');
            require(tokens.size() == 3 && identifier(tokens[0]) && identifier(tokens[2]) &&
                        (tokens[1] == "RP" || tokens[1] == "BR") &&
                        report.ref.size() - slash - 1 <= 64 && refs.insert(report.ref).second,
                    "invalid or duplicate RCB reference");
            report.dataset = dataset_ref(item.at("dataset_ref").get<std::string>());
            report.required = item.value("required", true);
            report.gi = item.value("gi_on_connect", true);
            report.integrity = integer(item, "integrity_period_ms", 10000, 1000, 86400000);
            if (item.contains("trigger_options")) {
                require(item.at("trigger_options").is_array(), "invalid trigger_options");
                report.triggers = 0;
                const std::map<std::string, int> bits{{"dchg", TRG_OPT_DATA_CHANGED},
                                                      {"qchg", TRG_OPT_QUALITY_CHANGED},
                                                      {"dupd", TRG_OPT_DATA_UPDATE},
                                                      {"integrity", TRG_OPT_INTEGRITY},
                                                      {"gi", TRG_OPT_GI}};
                for (const auto& name : item.at("trigger_options")) {
                    const auto found = bits.find(name.get<std::string>());
                    require(found != bits.end(), "invalid report trigger");
                    report.triggers |= found->second;
                }
            }
            require(!report.gi || (report.triggers & TRG_OPT_GI), "GI requires gi trigger");
            require(report.ref.find(".BR.") == std::string::npos || report.gi ||
                        (report.triggers & TRG_OPT_INTEGRITY),
                    "BRCB requires GI or integrity for a current-value baseline");
            require(config.read_mode != "report" || report.required,
                    "optional reports require hybrid mode");
            if (std::find(config.datasets.begin(), config.datasets.end(), report.dataset) ==
                config.datasets.end())
                config.datasets.push_back(report.dataset);
            if (report.required) config.required_datasets.insert(report.dataset);
            config.reports.push_back(std::move(report));
        }
    }
    require(config.read_mode == "poll" ? config.reports.empty() : !config.reports.empty(),
            "reports and read_mode do not match");
    return config;
}

struct Target {
    std::string ref;
    FunctionalConstraint fc;
    MmsVariableSpecification* spec{nullptr};  // 借用 roots 中的规格。
};

struct Projection {
    std::string target;
    std::vector<int> path;
};

struct Member {
    Target target;
    std::vector<Projection> projections;
};

struct Dataset {
    std::string ref;
    std::vector<std::string> directory;
    std::vector<Member> members;
};

std::vector<std::string> directory(IedConnection connection, const std::string& ref) {
    IedClientError error = IED_ERROR_OK;
    std::unique_ptr<sLinkedList, decltype(&LinkedList_destroy)> list(
        IedConnection_getDataSetDirectory(connection, &error, ref.c_str(), nullptr),
        LinkedList_destroy);
    require(error == IED_ERROR_OK && list,
            "DataSet directory failed: " + ref + " ied_error=" + std::to_string(error));
    std::vector<std::string> result;
    for (auto entry = LinkedList_getNext(list.get()); entry; entry = LinkedList_getNext(entry)) {
        require(result.size() < 65536, "DataSet member limit exceeded");
        const auto* data = static_cast<const char*>(LinkedList_getData(entry));
        require(data && std::strlen(data) <= 160, "invalid DataSet directory member");
        result.emplace_back(data);
    }
    return result;
}

Target member_target(const std::string& text) {
    const auto bracket = text.rfind('[');
    require(bracket != std::string::npos && text.back() == ']' && bracket + 4 == text.size(),
            "DataSet member has no explicit FC: " + text);
    Target target{text.substr(0, bracket), constraint(text.substr(bracket + 1, 2)), nullptr};
    reference(target.ref);
    return target;
}

MmsVariableSpecification* descend(MmsVariableSpecification* spec, const std::vector<Step>& steps,
                                  std::vector<int>& path, bool root) {
    for (std::size_t i = 0; i < steps.size(); ++i) {
        if (!root || i != 0) {
            int index = -1;
            require(spec && MmsVariableSpecification_getType(spec) == MMS_STRUCTURE,
                    "object path is not a structure");
            spec = MmsVariableSpecification_getChildSpecificationByName(spec, steps[i].name.c_str(),
                                                                        &index);
            require(spec && index >= 0, "model child does not exist: " + steps[i].name);
            path.push_back(index);
        }
        if (steps[i].index >= 0) {
            require(spec && MmsVariableSpecification_getType(spec) == MMS_ARRAY &&
                        steps[i].index < MmsVariableSpecification_getSize(spec),
                    "array index out of range");
            path.push_back(steps[i].index);
            spec = MmsVariableSpecification_getArrayElementSpecification(spec);
        }
    }
    return spec;
}

bool scalar(MmsType type) {
    return type == MMS_BOOLEAN || type == MMS_INTEGER || type == MMS_UNSIGNED || type == MMS_FLOAT;
}

std::string type_name(MmsVariableSpecification* spec) {
    switch (MmsVariableSpecification_getType(spec)) {
        case MMS_BOOLEAN:
            return "boolean";
        case MMS_INTEGER:
            return "integer";
        case MMS_UNSIGNED:
            return "unsigned";
        case MMS_FLOAT:
            return MmsVariableSpecification_getSize(spec) == 32 ? "float32" : "float64";
        default:
            return "unsupported";
    }
}

double number(MmsValue* value, MmsVariableSpecification* spec, Json& result) {
    const auto type = MmsValue_getType(value);
    if (type == MMS_BOOLEAN) return MmsValue_getBoolean(value) ? 1.0 : 0.0;
    if (type == MMS_FLOAT) {
        const double decoded = MmsValue_toDouble(value);
        require(std::isfinite(decoded), "non_finite_value");
        return decoded;
    }
    require(type == MMS_INTEGER || type == MMS_UNSIGNED, "non_numeric_value");
    // 公开 API 的 toUint32 会截断大整数；从有界 BER 编码解出完整整数。
    const int size = MmsValue_encodeMmsData(value, nullptr, 0, false);
    require(size >= 3 && size <= 11, "integer_encoding_out_of_range");
    std::array<std::uint8_t, 16> encoded{};
    require(
        MmsValue_encodeMmsData(value, encoded.data(), 0, true) == size && encoded[1] == size - 2,
        "invalid_integer_encoding");
    int start = 2;
    if (size == 11) {
        require(type == MMS_UNSIGNED && encoded[start++] == 0, "integer_encoding_out_of_range");
    }
    std::uint64_t raw = type == MMS_INTEGER && (encoded[start] & 0x80) ? UINT64_MAX : 0;
    for (int i = start; i < size; ++i) raw = (raw << 8) | encoded[i];
    const int width = MmsVariableSpecification_getSize(spec);
    constexpr std::uint64_t exact = 9007199254740991ULL;
    if (type == MMS_UNSIGNED) {
        require(!(encoded[2] & 0x80) && width > 0 && width <= 64 &&
                    (width == 64 || raw < (std::uint64_t{1} << width)),
                "unsigned_value_out_of_range");
        result["raw_integer"] = std::to_string(raw);
        require(raw <= exact, "integer_precision_loss");
        return static_cast<double>(raw);
    }
    std::int64_t signed_value;
    std::memcpy(&signed_value, &raw, sizeof(raw));
    require(width > 0 && width <= 64 &&
                (width == 64 || (signed_value >= -(std::int64_t{1} << (width - 1)) &&
                                 signed_value < (std::int64_t{1} << (width - 1)))),
            "integer_value_out_of_range");
    result["raw_integer"] = std::to_string(signed_value);
    require(signed_value >= -static_cast<std::int64_t>(exact) &&
                signed_value <= static_cast<std::int64_t>(exact),
            "integer_precision_loss");
    return static_cast<double>(signed_value);
}
}  // namespace

class Iec61850Protocol::Impl {
   public:
    Config config;
    bool initialized{false};
    IedConnection connection{nullptr};
    std::string error;
    std::map<std::string, Spec> roots;
    std::map<std::string, Target> targets;
    std::map<std::string, std::size_t> points;
    std::vector<Dataset> datasets;
    std::size_t model_nodes{0};

    std::unique_ptr<MmsReadOnlyServer> server;
    mutable std::mutex io_mutex;
    std::map<std::string, Sample> cache;
    std::string report_warning;

    struct Event {
        std::size_t subscription{0};
        std::uint16_t sequence{0}, sub_sequence{0};
        bool has_sequence{false}, segmented{false}, more{false}, overflow{false};
        std::uint32_t conf_rev{0};
        bool has_conf_rev{false};
        std::string failure;
        bool baseline_reasons{true};
        std::vector<std::size_t> included;
        std::map<std::string, Sample> updates;
        std::size_t bytes{0};
    };

    struct Subscription {
        Impl* owner{nullptr};
        std::size_t index{0}, dataset_index{0};
        ReportConfig config;
        std::string rpt_id;
        bool id_claimed{false};
        ClientReportControlBlock rcb{nullptr};
        bool installed{false}, reserved{false}, enabled{false}, buffered{false};
        std::uint32_t conf_rev{0};
        bool sequence_known{false};
        std::uint16_t last_sequence{0}, next_segment{0};
        std::uint16_t segment_sequence{0};
        std::size_t segment_bytes{0};
        std::uint64_t resync_count{0};
        std::string last_resync_reason;
        bool awaiting_baseline{false}, segment_baseline{true};
        std::set<std::size_t> segment_included;
        bool assembling{false};
        Clock::time_point segment_deadline{};
        std::map<std::string, Sample> segments;

        ~Subscription() {
            if (rcb) ClientReportControlBlock_destroy(rcb);
        }
    };

    std::vector<std::unique_ptr<Subscription>> subscriptions;
    std::mutex queue_mutex;
    std::condition_variable wake;
    std::deque<Event> events;
    std::set<std::size_t> lost;
    std::size_t queued_bytes{0};
    std::size_t assembly_bytes{0};
    bool accepting{false};
    std::atomic<bool> stopping{false};
    bool resync{false}, fatal_report{false};
    std::thread worker;
    int poll_batch(const std::vector<std::string>& codes, std::string& output,
                   bool cache_only = false, Clock::time_point deadline = Clock::time_point::max());
    int render(const std::vector<std::string>& codes, const std::map<std::string, Sample>& samples,
               std::string& output);
    static void report_callback(void* parameter, ClientReport report) noexcept;
    void start_reports(Clock::time_point deadline);
    void stop_reports();
    void drain_reports();
    void invalidate(Subscription& subscription, const std::string& reason);
    bool refresh(Clock::time_point deadline);
    void run_reports() noexcept;

    ~Impl() { close(); }

    void close() {
        stop_reports();
        if (server) server->disconnect();
        cache.clear();
        datasets.clear();
        targets.clear();
        roots.clear();
        points.clear();
        if (connection) {
            IedConnection_close(connection);
            IedConnection_destroy(connection);
            connection = nullptr;
        }
    }

    bool connected() const {
        if (server) return server->is_connected();
        return connection && IedConnection_getState(connection) == IED_STATE_CONNECTED;
    }

    void budget(Clock::time_point deadline) {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        require(remaining.count() > 0, "request_budget_exceeded");
        IedConnection_setRequestTimeout(
            connection,
            static_cast<std::uint32_t>(std::min<std::int64_t>(config.timeout, remaining.count())));
    }

    void count_spec(MmsVariableSpecification* spec, int depth) {
        require(spec && depth <= config.max_depth &&
                    ++model_nodes <= static_cast<std::size_t>(config.max_nodes),
                "model resource limit exceeded");
        const auto type = MmsVariableSpecification_getType(spec);
        if (type == MMS_ARRAY) {
            require(MmsVariableSpecification_getSize(spec) >= 0 &&
                        MmsVariableSpecification_getSize(spec) <= config.max_nodes,
                    "model array size limit exceeded");
            count_spec(MmsVariableSpecification_getArrayElementSpecification(spec), depth + 1);
        } else if (type == MMS_STRUCTURE) {
            const int count = MmsVariableSpecification_getSize(spec);
            require(count >= 0 && count <= config.max_nodes, "model structure limit exceeded");
            for (int i = 0; i < count; ++i)
                count_spec(MmsVariableSpecification_getChildSpecificationByIndex(spec, i),
                           depth + 1);
        }
    }

    Target lookup(const std::string& ref, FunctionalConstraint fc, Clock::time_point deadline) {
        const auto parsed = reference(ref);
        const auto root_key = key(parsed.base, fc);
        auto found = roots.find(root_key);
        if (found == roots.end()) {
            budget(deadline);
            IedClientError code = IED_ERROR_OK;
            Spec spec(
                IedConnection_getVariableSpecification(connection, &code, parsed.base.c_str(), fc),
                MmsVariableSpecification_destroy);
            require(code == IED_ERROR_OK && spec,
                    "model lookup failed: " + root_key + " ied_error=" + std::to_string(code));
            count_spec(spec.get(), 1);
            found = roots.emplace(root_key, std::move(spec)).first;
        }
        std::vector<int> path;
        auto* spec = descend(found->second.get(), parsed.steps, path, true);
        require(spec != nullptr, "model object is absent: " + ref);
        return {ref, fc, spec};
    }

    void add_target(const std::string& ref, FunctionalConstraint fc, Clock::time_point deadline) {
        if (!ref.empty() && !targets.count(key(ref, fc)))
            targets.emplace(key(ref, fc), lookup(ref, fc, deadline));
    }

    void prepare(Clock::time_point deadline) {
        model_nodes = 0;
        for (std::size_t i = 0; i < config.points.size(); ++i) {
            budget(deadline);
            const auto& point = config.points[i];
            add_target(point.ref, point.fc, deadline);
            auto* spec = targets.at(key(point.ref, point.fc)).spec;
            require(scalar(MmsVariableSpecification_getType(spec)),
                    "point is not a numeric leaf: " + point.ref);
            require(point.type.empty() || type_name(spec) == point.type,
                    "MMS type mismatch: " + point.ref);
            add_target(point.quality, point.fc, deadline);
            add_target(point.timestamp, point.fc, deadline);
            if (!point.quality.empty()) {
                auto* quality = targets.at(key(point.quality, point.fc)).spec;
                const int bits = MmsVariableSpecification_getSize(quality);
                require(MmsVariableSpecification_getType(quality) == MMS_BIT_STRING &&
                            ((bits >= 13 && bits <= 16) || (bits >= -16 && bits <= -13)),
                        "quality_ref is not an IEC quality: " + point.quality);
            }
            if (!point.timestamp.empty())
                require(MmsVariableSpecification_getType(
                            targets.at(key(point.timestamp, point.fc)).spec) == MMS_UTC_TIME,
                        "timestamp_ref is not UTC time: " + point.timestamp);
            points.emplace(point.code, i);
        }
        for (const auto& ref : config.datasets) {
            try {
                budget(deadline);
                Dataset dataset;
                dataset.ref = ref;
                dataset.directory = directory(connection, ref);
                for (const auto& entry : dataset.directory) {
                    budget(deadline);
                    auto member = member_target(entry);
                    member = lookup(member.ref, member.fc, deadline);
                    Member compiled{member, {}};
                    const auto member_key = key(member.ref, member.fc);
                    for (auto candidate = targets.lower_bound(member_key);
                         candidate != targets.end() &&
                         candidate->first.compare(0, member_key.size(), member_key) == 0;
                         ++candidate) {
                        const auto& [target_key, target] = *candidate;
                        if (target.fc != member.fc) continue;
                        if (target.ref != member.ref &&
                            (target.ref.size() <= member.ref.size() ||
                             target.ref.compare(0, member.ref.size(), member.ref) != 0 ||
                             (target.ref[member.ref.size()] != '.' &&
                              target.ref[member.ref.size()] != '(')))
                            continue;
                        // 从共同 FCD 根构建两条模型索引路径，再取后缀，避免靠字段位置猜测。
                        const auto target_parsed = reference(target.ref);
                        const auto member_parsed = reference(member.ref);
                        if (target_parsed.base != member_parsed.base) continue;
                        std::vector<int> member_path, target_path;
                        auto* root = roots.at(key(target_parsed.base, target.fc)).get();
                        descend(root, member_parsed.steps, member_path, true);
                        descend(root, target_parsed.steps, target_path, true);
                        if (member_path.size() > target_path.size() ||
                            !std::equal(member_path.begin(), member_path.end(),
                                        target_path.begin()))
                            continue;
                        compiled.projections.push_back(
                            {target_key, std::vector<int>(target_path.begin() + member_path.size(),
                                                          target_path.end())});
                    }
                    dataset.members.push_back(std::move(compiled));
                }
                datasets.push_back(std::move(dataset));
            } catch (const std::exception& failure) {
                if (config.required_datasets.count(ref)) throw;
                report_warning += ref + ": " + failure.what() + "; ";
                if (report_warning.size() > 2048) report_warning.resize(2048);
            }
        }
        require(Clock::now() < deadline, "model discovery budget exceeded");
    }
};

void Iec61850Protocol::Impl::report_callback(void* parameter, ClientReport report) noexcept {
    auto& subscription = *static_cast<Subscription*>(parameter);
    auto& owner = *subscription.owner;
    try {
        {
            std::lock_guard<std::mutex> lock(owner.queue_mutex);
            if (!owner.accepting) return;
        }
        Event event;
        event.subscription = subscription.index;
        event.has_sequence = ClientReport_hasSeqNum(report);
        if (event.has_sequence) event.sequence = ClientReport_getSeqNum(report);
        event.has_conf_rev = ClientReport_hasConfRev(report);
        if (event.has_conf_rev) event.conf_rev = ClientReport_getConfRev(report);
        event.segmented = ClientReport_hasSubSeqNum(report);
        if (event.segmented) {
            event.sub_sequence = ClientReport_getSubSeqNum(report);
            event.more = ClientReport_getMoreSeqmentsFollow(report);
        }
        event.overflow = ClientReport_hasBufOvfl(report) && ClientReport_getBufOvfl(report);
        Json diagnostic{{"rcb_ref", subscription.config.ref}, {"buffered", subscription.buffered}};
        if (event.has_sequence) diagnostic["sequence"] = event.sequence;
        if (event.has_conf_rev) diagnostic["conf_rev"] = event.conf_rev;
        if (event.segmented) {
            diagnostic["sub_sequence"] = event.sub_sequence;
            diagnostic["more_segments"] = event.more;
        }
        if (ClientReport_hasBufOvfl(report)) diagnostic["buffer_overflow"] = event.overflow;
        if (ClientReport_hasTimestamp(report))
            diagnostic["time_of_entry"] = ClientReport_getTimestamp(report);
        if (const auto* id = ClientReport_getRptId(report))
            diagnostic["rpt_id"] = std::string(id).substr(0, 129);
        if (auto* entry = ClientReport_getEntryId(report)) {
            const int count = MmsValue_getOctetStringSize(entry);
            require(count <= 64, "report EntryID limit exceeded");
            std::string hex;
            constexpr char digits[] = "0123456789ABCDEF";
            for (int i = 0; i < count; ++i) {
                const auto byte = MmsValue_getOctetStringOctet(entry, i);
                hex += digits[byte >> 4];
                hex += digits[byte & 15];
            }
            diagnostic["entry_id"] = hex;
        }
        const auto& dataset = owner.datasets.at(subscription.dataset_index);
        if (!ClientReport_hasDataSetName(report) || !ClientReport_getDataSetName(report) ||
            dataset_ref(ClientReport_getDataSetName(report)) != dataset.ref)
            event.failure = "report_dataset_mismatch";
        auto* values = ClientReport_getDataSetValues(report);
        if (!values || MmsValue_getArraySize(values) != dataset.members.size())
            event.failure = "report_member_count_changed";
        const auto received = now_ms();
        const auto arrival = Clock::now();
        const auto group = subscription.config.ref + ":" + std::to_string(event.sequence);
        if (event.failure.empty()) {
            for (std::size_t i = 0; i < dataset.members.size(); ++i) {
                const int reason = ClientReport_getReasonForInclusion(report, static_cast<int>(i));
                if (reason == IEC61850_REASON_NOT_INCLUDED) continue;
                event.included.push_back(i);
                event.bytes += sizeof(std::size_t);
                event.baseline_reasons =
                    event.baseline_reasons &&
                    (reason & (IEC61850_REASON_GI | IEC61850_REASON_INTEGRITY));
                const auto& member = dataset.members[i];
                auto* value = MmsValue_getElement(values, static_cast<int>(i));
                const bool valid =
                    value && MmsVariableSpecification_isValueOfType(member.target.spec, value);
                for (const auto& projection : member.projections) {
                    auto* leaf = valid ? value : nullptr;
                    for (const int index : projection.path)
                        if (leaf) leaf = MmsValue_getElement(leaf, index);
                    Sample sample{
                        nullptr,  "report", group, leaf ? "" : "report_member_type_or_access_error",
                        received, arrival,  {},    {}};
                    if (leaf) {
                        require(MmsValue_getSizeInMemory(leaf) <= 256,
                                "report scalar size limit exceeded");
                        sample.owned =
                            std::shared_ptr<MmsValue>(MmsValue_clone(leaf), MmsValue_delete);
                        require(sample.owned != nullptr, "cannot copy report scalar");
                        sample.value = sample.owned.get();
                    }
                    sample.report = diagnostic;
                    sample.report["reason"] = reason;
                    event.bytes +=
                        projection.target.size() + sizeof(Sample) + diagnostic.dump().size() + 256;
                    require(event.bytes <=
                                static_cast<std::size_t>(enum_value(PluginLimits::MaxResultBytes)),
                            "report byte limit exceeded");
                    event.updates[projection.target] = std::move(sample);
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(owner.queue_mutex);
            if (!owner.accepting) return;
            if (owner.events.size() >= static_cast<std::size_t>(owner.config.queue_size) ||
                owner.queued_bytes + event.bytes >
                    static_cast<std::size_t>(enum_value(PluginLimits::MaxResultBytes))) {
                owner.lost.insert(subscription.index);
            } else {
                owner.queued_bytes += event.bytes;
                owner.events.push_back(std::move(event));
            }
        }
        owner.wake.notify_one();
    } catch (...) {
        try {
            std::lock_guard<std::mutex> lock(owner.queue_mutex);
            if (owner.accepting) owner.lost.insert(subscription.index);
            owner.wake.notify_one();
        } catch (...) {
        }
    }
}

void Iec61850Protocol::Impl::stop_reports() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        accepting = false;
        stopping = true;
    }
    wake.notify_all();
    if (worker.joinable()) worker.join();
    for (auto& subscription : subscriptions) {
        if (!connection) break;
        if (IedConnection_getState(connection) == IED_STATE_CONNECTED && subscription->rcb) {
            IedConnection_setRequestTimeout(
                connection, static_cast<std::uint32_t>(std::min(config.timeout, 1000)));
            IedClientError ignored = IED_ERROR_OK;
            if (subscription->enabled) {
                ClientReportControlBlock_setRptEna(subscription->rcb, false);
                IedConnection_setRCBValues(connection, &ignored, subscription->rcb,
                                           RCB_ELEMENT_RPT_ENA, true);
            }
            if (subscription->reserved) {
                if (subscription->buffered)
                    ClientReportControlBlock_setResvTms(subscription->rcb, 0);
                else
                    ClientReportControlBlock_setResv(subscription->rcb, false);
                IedConnection_setRCBValues(
                    connection, &ignored, subscription->rcb,
                    subscription->buffered ? RCB_ELEMENT_RESV_TMS : RCB_ELEMENT_RESV, true);
            }
        }
        // 底层函数等待持锁的接收回调退出；此处不能持有 queue_mutex。
        if (subscription->installed)
            IedConnection_uninstallReportHandler(connection, subscription->config.ref.c_str());
    }
    subscriptions.clear();
    assembly_bytes = 0;
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        events.clear();
        lost.clear();
        queued_bytes = 0;
    }
    resync = false;
    fatal_report = false;
    report_warning.clear();
}

void Iec61850Protocol::Impl::start_reports(Clock::time_point deadline) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        accepting = true;
    }
    std::set<std::string> ids, coverage;
    for (const auto& configured : config.reports) {
        auto runtime = std::make_unique<Subscription>();
        runtime->owner = this;
        runtime->index = subscriptions.size();
        runtime->config = configured;
        const auto dataset = std::find_if(datasets.begin(), datasets.end(), [&](const auto& item) {
            return item.ref == configured.dataset;
        });
        if (dataset == datasets.end() && !configured.required) continue;
        require(dataset != datasets.end(), "report DataSet has not been prepared");
        runtime->dataset_index = static_cast<std::size_t>(dataset - datasets.begin());
        subscriptions.push_back(std::move(runtime));
        auto& subscription = *subscriptions.back();
        try {
            budget(deadline);
            IedClientError status = IED_ERROR_OK;
            subscription.rcb =
                IedConnection_getRCBValues(connection, &status, configured.ref.c_str(), nullptr);
            require(
                status == IED_ERROR_OK && subscription.rcb,
                "getRCBValues failed: " + configured.ref + " ied_error=" + std::to_string(status));
            auto* rcb = subscription.rcb;
            subscription.buffered = ClientReportControlBlock_isBuffered(rcb);
            subscription.awaiting_baseline = subscription.buffered;
            require(!ClientReportControlBlock_getRptEna(rcb),
                    "RCB is already enabled: " + configured.ref);
            const auto* owner = ClientReportControlBlock_getOwner(rcb);
            if (owner) {
                for (int i = 0; i < MmsValue_getOctetStringSize(owner); ++i)
                    require(MmsValue_getOctetStringOctet(const_cast<MmsValue*>(owner), i) == 0,
                            "RCB is owned by another association");
            }
            const auto* remote_dataset = ClientReportControlBlock_getDataSetReference(rcb);
            require(remote_dataset && dataset_ref(remote_dataset) == configured.dataset,
                    "RCB DataSet mismatch");
            if (!subscription.buffered)
                require(!ClientReportControlBlock_getResv(rcb), "URCB is reserved");
            if (subscription.buffered && ClientReportControlBlock_hasResvTms(rcb))
                require(ClientReportControlBlock_getResvTms(rcb) == 0, "BRCB is reserved");
            const auto* remote_id = ClientReportControlBlock_getRptId(rcb);
            std::string id = remote_id ? remote_id : "";
            if (id.empty()) {
                id = configured.ref;
                std::replace(id.begin(), id.end(), '.', '$');
            }
            require(ids.insert(id).second, "ambiguous RptId in one MMS association");
            subscription.rpt_id = id;
            subscription.id_claimed = true;
            subscription.conf_rev = ClientReportControlBlock_getConfRev(rcb);
            if (!subscription.buffered || ClientReportControlBlock_hasResvTms(rcb)) {
                if (subscription.buffered)
                    ClientReportControlBlock_setResvTms(rcb, 1);
                else
                    ClientReportControlBlock_setResv(rcb, true);
                budget(deadline);
                subscription.reserved = true;
                IedConnection_setRCBValues(
                    connection, &status, rcb,
                    subscription.buffered ? RCB_ELEMENT_RESV_TMS : RCB_ELEMENT_RESV, true);
                require(status == IED_ERROR_OK, "RCB reservation failed");
            }
            const int options =
                RPT_OPT_SEQ_NUM | RPT_OPT_TIME_STAMP | RPT_OPT_REASON_FOR_INCLUSION |
                RPT_OPT_DATA_SET | RPT_OPT_CONF_REV |
                (subscription.buffered ? RPT_OPT_BUFFER_OVERFLOW | RPT_OPT_ENTRY_ID : 0);
            ClientReportControlBlock_setTrgOps(rcb, configured.triggers);
            ClientReportControlBlock_setIntgPd(rcb, configured.integrity);
            ClientReportControlBlock_setOptFlds(rcb, options);
            budget(deadline);
            IedConnection_setRCBValues(
                connection, &status, rcb,
                RCB_ELEMENT_TRG_OPS | RCB_ELEMENT_INTG_PD | RCB_ELEMENT_OPT_FLDS, true);
            require(status == IED_ERROR_OK, "RCB trigger/options configuration failed");
            budget(deadline);
            IedConnection_getRCBValues(connection, &status, configured.ref.c_str(), rcb);
            require(status == IED_ERROR_OK &&
                        ClientReportControlBlock_getTrgOps(rcb) == configured.triggers &&
                        ClientReportControlBlock_getIntgPd(rcb) ==
                            static_cast<std::uint32_t>(configured.integrity) &&
                        (ClientReportControlBlock_getOptFlds(rcb) & options) == options,
                    "RCB readback mismatch");
            IedConnection_installReportHandler(connection, configured.ref.c_str(), id.c_str(),
                                               report_callback, &subscription);
            subscription.installed = true;
            ClientReportControlBlock_setRptEna(rcb, true);
            budget(deadline);
            subscription.enabled = true;
            IedConnection_setRCBValues(connection, &status, rcb, RCB_ELEMENT_RPT_ENA, true);
            require(status == IED_ERROR_OK, "RCB enable failed");
            if (configured.gi) {
                ClientReportControlBlock_setGI(rcb, true);
                budget(deadline);
                IedConnection_setRCBValues(connection, &status, rcb, RCB_ELEMENT_GI, true);
                require(status == IED_ERROR_OK, "RCB GI failed");
            }
            for (const auto& member : dataset->members)
                for (const auto& projection : member.projections)
                    coverage.insert(projection.target);
        } catch (const std::exception& failure) {
            if (configured.required) throw;
            report_warning += configured.ref + ": " + failure.what() + "; ";
            if (report_warning.size() > 2048) report_warning.resize(2048);
            if (subscription.installed) {
                IedConnection_uninstallReportHandler(connection, configured.ref.c_str());
                subscription.installed = false;
            }
            if (subscription.rcb && IedConnection_getState(connection) == IED_STATE_CONNECTED) {
                IedConnection_setRequestTimeout(
                    connection, static_cast<std::uint32_t>(std::min(config.timeout, 1000)));
                IedClientError ignored;
                if (subscription.enabled) {
                    ClientReportControlBlock_setRptEna(subscription.rcb, false);
                    IedConnection_setRCBValues(connection, &ignored, subscription.rcb,
                                               RCB_ELEMENT_RPT_ENA, true);
                    require(ignored == IED_ERROR_OK, "cannot disable optional RCB after failure");
                }
                if (subscription.reserved) {
                    if (subscription.buffered)
                        ClientReportControlBlock_setResvTms(subscription.rcb, 0);
                    else
                        ClientReportControlBlock_setResv(subscription.rcb, false);
                    IedConnection_setRCBValues(
                        connection, &ignored, subscription.rcb,
                        subscription.buffered ? RCB_ELEMENT_RESV_TMS : RCB_ELEMENT_RESV, true);
                    require(ignored == IED_ERROR_OK, "cannot release optional RCB after failure");
                }
            }
            subscription.enabled = false;
            subscription.reserved = false;
            if (subscription.id_claimed) {
                ids.erase(subscription.rpt_id);
                subscription.id_claimed = false;
            }
        }
    }
    if (config.read_mode == "report")
        for (const auto& [target, _] : targets)
            require(coverage.count(target) != 0, "report mode target is not covered: " + target);
}

void Iec61850Protocol::Impl::invalidate(Subscription& subscription, const std::string& reason) {
    ++subscription.resync_count;
    subscription.last_resync_reason = reason;
    const auto& dataset = datasets.at(subscription.dataset_index);
    for (const auto& member : dataset.members)
        for (const auto& projection : member.projections) {
            auto& sample = cache[projection.target];
            sample.error = reason;
            sample.report = Json{{"rcb_ref", subscription.config.ref},
                                 {"resync_reason", reason},
                                 {"resync_count", subscription.resync_count},
                                 {"last_resync_reason", reason}};
        }
    subscription.assembling = false;
    subscription.sequence_known = false;
    subscription.segments.clear();
    assembly_bytes -= std::min(assembly_bytes, subscription.segment_bytes);
    subscription.segment_included.clear();
    subscription.awaiting_baseline = subscription.buffered;
    subscription.segment_bytes = 0;
    resync = true;
}

void Iec61850Protocol::Impl::drain_reports() {
    std::deque<Event> pending;
    std::set<std::size_t> missing;
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        pending.swap(events);
        missing.swap(lost);
        queued_bytes = 0;
    }
    for (const auto index : missing)
        if (index < subscriptions.size())
            invalidate(*subscriptions[index], "report_queue_overflow_or_copy_failure");
    for (auto& event : pending) {
        if (fatal_report) continue;
        auto& subscription = *subscriptions.at(event.subscription);
        if (!subscription.enabled || missing.count(event.subscription)) continue;
        if (!event.failure.empty() || !event.has_conf_rev ||
            event.conf_rev != subscription.conf_rev) {
            invalidate(subscription,
                       event.failure.empty() ? "report_conf_rev_changed" : event.failure);
            fatal_report = true;
            report_warning = "report model changed; reconnect required: " +
                             (event.failure.empty() ? std::string("conf_rev") : event.failure);
            continue;
        }
        if (event.overflow || !event.has_sequence) {
            invalidate(subscription,
                       event.overflow ? "report_buffer_overflow" : "report_sequence_missing");
            continue;
        }
        if (event.segmented) {
            if (event.sub_sequence == 0) {
                if (subscription.assembling) invalidate(subscription, "report_segment_restarted");
                subscription.assembling = true;
                subscription.next_segment = 0;
                subscription.segment_baseline = true;
                subscription.segment_included.clear();
                subscription.segment_sequence = event.sequence;
                subscription.segment_deadline =
                    Clock::now() + std::chrono::milliseconds(config.timeout);
            }
            if (!subscription.assembling || event.sequence != subscription.segment_sequence ||
                event.sub_sequence != subscription.next_segment ||
                Clock::now() > subscription.segment_deadline) {
                invalidate(subscription, "report_segment_missing_or_expired");
                continue;
            }
            ++subscription.next_segment;
            if (assembly_bytes + event.bytes >
                static_cast<std::size_t>(enum_value(PluginLimits::MaxResultBytes))) {
                invalidate(subscription, "report_assembly_byte_limit_exceeded");
                continue;
            }
            bool duplicate = false;
            for (const auto member : event.included)
                if (subscription.segment_included.count(member)) duplicate = true;
            if (duplicate) {
                invalidate(subscription, "report_segment_duplicate_member");
                continue;
            }
            assembly_bytes += event.bytes;
            subscription.segment_bytes += event.bytes;
            if ((event.more && subscription.next_segment == 0) ||
                subscription.segment_bytes >
                    static_cast<std::size_t>(enum_value(PluginLimits::MaxResultBytes))) {
                invalidate(subscription, "report_segment_limit_exceeded");
                continue;
            }
            for (auto& [target, sample] : event.updates)
                subscription.segments[target] = std::move(sample);
            subscription.segment_included.insert(event.included.begin(), event.included.end());
            subscription.segment_baseline = subscription.segment_baseline && event.baseline_reasons;
            if (event.more) continue;
            event.updates.swap(subscription.segments);
            event.included.assign(subscription.segment_included.begin(),
                                  subscription.segment_included.end());
            event.baseline_reasons = subscription.segment_baseline;
            subscription.segment_included.clear();
            subscription.segments.clear();
            assembly_bytes -= std::min(assembly_bytes, subscription.segment_bytes);
            subscription.segment_bytes = 0;
            subscription.assembling = false;
        } else if (subscription.assembling) {
            invalidate(subscription, "report_segment_interrupted");
        }
        const unsigned mask = subscription.buffered ? 65535 : 255;
        const bool gap = subscription.sequence_known &&
                         event.sequence != ((subscription.last_sequence + 1U) & mask);
        subscription.last_sequence = event.sequence;
        subscription.sequence_known = true;
        if (gap) {
            invalidate(subscription, "report_sequence_gap");
            continue;
        }
        if (subscription.awaiting_baseline) {
            if (!event.baseline_reasons ||
                event.included.size() != datasets.at(subscription.dataset_index).members.size())
                continue;
            subscription.awaiting_baseline = false;
        }
        for (auto& [target, sample] : event.updates) {
            sample.report["resync_count"] = subscription.resync_count;
            if (!subscription.last_resync_reason.empty())
                sample.report["last_resync_reason"] = subscription.last_resync_reason;
            const auto previous = cache.find(target);
            if (previous == cache.end() || previous->second.source != "report" ||
                previous->second.arrival <= sample.arrival)
                cache[target] = std::move(sample);
        }
    }
    for (auto& subscription : subscriptions)
        if (subscription->assembling && Clock::now() > subscription->segment_deadline)
            invalidate(*subscription, "report_segment_timeout");
    if (fatal_report && connection) IedConnection_close(connection);
}

bool Iec61850Protocol::Impl::refresh(Clock::time_point deadline) {
    const bool recovering = resync;
    std::vector<std::string> codes;
    for (const auto& point : config.points) codes.push_back(point.code);
    std::string unused;
    const int result = poll_batch(codes, unused, true, deadline);
    if (result == 0) {
        resync = false;
        if (recovering)
            for (auto& subscription : subscriptions) {
                if (!subscription->enabled || !subscription->buffered || !subscription->config.gi)
                    continue;
                try {
                    budget(deadline);
                    IedClientError status;
                    ClientReportControlBlock_setGI(subscription->rcb, true);
                    IedConnection_setRCBValues(connection, &status, subscription->rcb,
                                               RCB_ELEMENT_GI, true);
                    if (status != IED_ERROR_OK) resync = true;
                } catch (const std::exception&) {
                    resync = true;
                }
            }
    }
    return result == 0;
}

void Iec61850Protocol::Impl::run_reports() noexcept {
    auto next_poll = Clock::now() + std::chrono::milliseconds(config.poll_interval);
    try {
        while (!stopping) {
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                wake.wait_for(lock, std::chrono::milliseconds(100),
                              [&] { return stopping || !events.empty() || !lost.empty(); });
            }
            if (stopping) break;
            std::lock_guard<std::mutex> lock(io_mutex);
            drain_reports();
            if (!connected() || fatal_report) continue;
            if (resync || (config.read_mode == "hybrid" && Clock::now() >= next_poll)) {
                refresh(Clock::now() + std::chrono::milliseconds(config.timeout));
                drain_reports();
                next_poll = Clock::now() + std::chrono::milliseconds(config.poll_interval);
            }
        }
    } catch (...) {
        try {
            std::lock_guard<std::mutex> lock(io_mutex);
            for (auto& [_, sample] : cache) sample.error = "report_worker_failed";
            if (connection) IedConnection_close(connection);
        } catch (...) {
        }
    }
}

Iec61850Protocol::Iec61850Protocol() : impl_(std::make_unique<Impl>()) {}

Iec61850Protocol::~Iec61850Protocol() = default;

int Iec61850Protocol::initialize(const std::string& text) {
    try {
        require(!impl_->connected(), "disconnect before reconfiguring IEC61850");
        auto candidate = configuration(text);
        std::unique_ptr<MmsReadOnlyServer> server;
        if (candidate.server)
            server = std::make_unique<MmsReadOnlyServer>(candidate.server_options, candidate.host,
                                                         candidate.port);
        impl_->close();
        impl_->server = std::move(server);
        impl_->config = std::move(candidate);
        impl_->initialized = true;
        impl_->error.clear();
        return 0;
    } catch (const std::exception& error) {
        impl_->error = error.what();
        return -1;
    }
}

int Iec61850Protocol::connect() {
    if (impl_->connected()) return 0;
    if (impl_->server) {
        const int result = impl_->server->connect();
        impl_->error = impl_->server->last_error();
        return result;
    }
    try {
        require(impl_->initialized, "initialize before connecting IEC61850");
        impl_->close();
        impl_->connection = IedConnection_create();
        require(impl_->connection != nullptr, "cannot allocate IedConnection");
        IedConnection_setConnectTimeout(impl_->connection, impl_->config.connect_timeout);
        IedConnection_setRequestTimeout(impl_->connection, impl_->config.timeout);
        IedClientError code = IED_ERROR_OK;
        IedConnection_connect(impl_->connection, &code, impl_->config.host.c_str(),
                              impl_->config.port);
        require(code == IED_ERROR_OK && impl_->connected(),
                "MMS connect failed: ied_error=" + std::to_string(code));
        const auto deadline =
            Clock::now() + std::chrono::milliseconds(impl_->config.discovery_timeout);
        impl_->prepare(deadline);
        if (impl_->config.read_mode != "poll") {
            impl_->stopping = false;
            impl_->start_reports(deadline);
            impl_->refresh(deadline);
            impl_->drain_reports();
            impl_->worker = std::thread([owner = impl_.get()] { owner->run_reports(); });
        }
        impl_->error.clear();
        return 0;
    } catch (const std::exception& error) {
        impl_->close();
        impl_->error = error.what();
        return -1;
    }
}

int Iec61850Protocol::disconnect() {
    impl_->close();
    impl_->error.clear();
    return 0;
}

bool Iec61850Protocol::is_connected() const { return impl_->connected(); }

int Iec61850Protocol::write(const std::string& code, double value) {
    std::lock_guard<std::mutex> lock(impl_->io_mutex);
    if (impl_->server) {
        const int result = impl_->server->write(code, value);
        impl_->error = impl_->server->last_error();
        return result;
    }
    impl_->error = "unsupported: IEC61850 client is read-only; command not sent";
    return -1;
}

std::string Iec61850Protocol::last_error() const {
    std::lock_guard<std::mutex> lock(impl_->io_mutex);
    return impl_->error;
}

int Iec61850Protocol::update_image(const std::string& samples_json) {
    std::lock_guard<std::mutex> lock(impl_->io_mutex);
    if (!impl_->server) {
        impl_->error = "image update requires an initialized server";
        return -1;
    }
    const int result = impl_->server->update_image(samples_json);
    impl_->error = impl_->server->last_error();
    return result;
}

int Iec61850Protocol::read_batch(const std::vector<std::string>& codes, std::string& output) {
    std::lock_guard<std::mutex> lock(impl_->io_mutex);
    if (impl_->server) {
        const int result = impl_->server->read_batch(codes, output);
        impl_->error = impl_->server->last_error();
        return result;
    }
    if (impl_->config.read_mode == "poll") return impl_->poll_batch(codes, output);
    if (!impl_->initialized || codes.size() > static_cast<std::size_t>(impl_->config.max_points)) {
        impl_->error = "invalid batch";
        return -1;
    }
    for (const auto& code : codes)
        if (code.size() > 256) {
            impl_->error = "point code exceeds 256 bytes";
            return -1;
        }
    impl_->drain_reports();
    return impl_->render(codes, impl_->cache, output);
}

int Iec61850Protocol::Impl::poll_batch(const std::vector<std::string>& codes, std::string& output,
                                       bool cache_only, Clock::time_point outer_deadline) {
    try {
        require(initialized && codes.size() <= static_cast<std::size_t>(config.max_points),
                "uninitialized instance or batch point limit exceeded");
        const auto deadline =
            std::min(outer_deadline, Clock::now() + std::chrono::milliseconds(config.timeout));
        const bool active = connected();
        std::set<std::string> requested;
        for (const auto& code : codes) {
            require(code.size() <= 256, "point code exceeds 256 bytes");
            auto point = points.find(code);
            if (point == points.end()) continue;
            const auto& p = config.points[point->second];
            requested.insert(key(p.ref, p.fc));
            if (!p.quality.empty()) requested.insert(key(p.quality, p.fc));
            if (!p.timestamp.empty()) requested.insert(key(p.timestamp, p.fc));
        }

        std::map<std::string, Sample> samples;
        std::vector<Value> values;
        std::vector<Data> data;
        if (active) {
            for (const auto& dataset : datasets) {
                bool needed = false;
                for (const auto& member : dataset.members)
                    for (const auto& projection : member.projections)
                        if (requested.count(projection.target) && !samples.count(projection.target))
                            needed = true;
                if (!needed) continue;
                auto received = now_ms();
                auto arrival = Clock::now();
                std::string failure;
                ClientDataSet result = nullptr;
                try {
                    budget(deadline);
                    IedClientError error = IED_ERROR_OK;
                    result = IedConnection_readDataSetValues(connection, &error,
                                                             dataset.ref.c_str(), nullptr);
                    if (result) data.emplace_back(result, ClientDataSet_destroy);
                    require(error == IED_ERROR_OK && result,
                            "dataset_read_failed: ied_error=" + std::to_string(error));
                    received = now_ms();
                    arrival = Clock::now();
                    require(MmsValue_getArraySize(ClientDataSet_getValues(result)) ==
                                dataset.members.size(),
                            "dataset_member_count_changed");
                    budget(deadline);
                    require(directory(connection, dataset.ref) == dataset.directory,
                            "dataset_directory_changed; reconnect required");
                } catch (const std::exception& error) {
                    failure = error.what();
                }
                for (std::size_t i = 0; i < dataset.members.size(); ++i) {
                    const auto& member = dataset.members[i];
                    MmsValue* member_value =
                        failure.empty() ? MmsValue_getElement(ClientDataSet_getValues(result),
                                                              static_cast<int>(i))
                                        : nullptr;
                    const bool valid = member_value && MmsVariableSpecification_isValueOfType(
                                                           member.target.spec, member_value);
                    for (const auto& projection : member.projections) {
                        if (!requested.count(projection.target)) continue;
                        auto* leaf = valid ? member_value : nullptr;
                        for (const int index : projection.path)
                            if (leaf) leaf = MmsValue_getElement(leaf, index);
                        samples.emplace(
                            projection.target,
                            Sample{leaf,
                                   "dataset",
                                   dataset.ref,
                                   failure.empty() && !leaf ? "dataset_member_type_or_access_error"
                                                            : failure,
                                   received,
                                   arrival,
                                   {},
                                   {}});
                    }
                }
            }
            for (const auto& target_key : requested) {
                if (samples.count(target_key)) continue;
                Sample sample;
                sample.source = "read";
                sample.group = target_key;
                try {
                    budget(deadline);
                    const auto& target = targets.at(target_key);
                    IedClientError error = IED_ERROR_OK;
                    auto* value =
                        IedConnection_readObject(connection, &error, target.ref.c_str(), target.fc);
                    if (value) values.emplace_back(value, MmsValue_delete);
                    require(error == IED_ERROR_OK && value,
                            "read_failed: ied_error=" + std::to_string(error));
                    require(MmsVariableSpecification_isValueOfType(target.spec, value),
                            "runtime_type_mismatch");
                    sample.value = value;
                } catch (const std::exception& error) {
                    sample.error = error.what();
                }
                sample.received = now_ms();
                sample.arrival = Clock::now();
                samples.emplace(target_key, std::move(sample));
            }
        }
        if (config.read_mode != "poll") {
            for (auto& [target, sample] : samples) {
                if (sample.value) {
                    sample.owned =
                        std::shared_ptr<MmsValue>(MmsValue_clone(sample.value), MmsValue_delete);
                    require(sample.owned != nullptr, "cannot copy cached sample");
                    sample.value = sample.owned.get();
                }
                const auto previous = cache.find(target);
                if (previous != cache.end() && previous->second.report.contains("resync_reason")) {
                    sample.report = previous->second.report;
                    sample.report["recovered"] = sample.value && sample.error.empty();
                }
                cache[target] = sample;
            }
        }
        if (cache_only) {
            output.clear();
            for (const auto& [_, sample] : samples)
                if (!sample.value || !sample.error.empty()) return -1;
            return active && connected() ? 0 : -1;
        }
        return render(codes, samples, output);
    } catch (const std::exception& failure) {
        output.clear();
        error = failure.what();
        return -1;
    }
}

int Iec61850Protocol::Impl::render(const std::vector<std::string>& codes,
                                   const std::map<std::string, Sample>& samples,
                                   std::string& output) {
    try {
        Json result = Json::array();
        int valid = 0;
        const bool still_connected = connected();
        std::size_t bytes = 2;
        for (const auto& code : codes) {
            Json item{{"code", code},
                      {"value", nullptr},
                      {"quality", 1},
                      {"timestamp", now_ms()},
                      {"timestamp_source", "received"},
                      {"quality_source", "unavailable"}};
            const auto configured = points.find(code);
            if (configured != points.end()) {
                const auto& point = config.points[configured->second];
                const auto found = samples.find(key(point.ref, point.fc));
                if (found != samples.end() && !found->second.report.is_null())
                    item["report"] = found->second.report;
            }
            try {
                require(still_connected, "disconnected");
                const auto found = points.find(code);
                require(found != points.end(), "point_not_configured");
                const auto& p = config.points[found->second];
                item["object_ref"] = p.ref;
                item["fc"] = FunctionalConstraint_toString(p.fc);
                auto& sample = samples.at(key(p.ref, p.fc));
                item["timestamp"] = sample.received;
                item["source"] = sample.source;
                item["mms_type"] = type_name(targets.at(key(p.ref, p.fc)).spec);
                require(sample.error.empty() && sample.value,
                        sample.error.empty() ? "missing_value" : sample.error);
                if (!sample.report.is_null()) item["report"] = sample.report;
                const double decoded =
                    number(sample.value, targets.at(key(p.ref, p.fc)).spec, item);
                bool coherent = true;
                bool good_quality = true;
                bool good_time = true;
                auto metadata = [&](const std::string& ref) -> MmsValue* {
                    auto& extra = samples.at(key(ref, p.fc));
                    coherent = coherent && sample.group == extra.group;
                    require(extra.value && extra.error.empty(),
                            "metadata_read_failed: " + ref + " " + extra.error);
                    if (!extra.report.is_null() && extra.arrival >= sample.arrival) {
                        item["report"] = extra.report;
                        item["source"] = extra.source;
                    }
                    return extra.value;
                };
                if (!p.quality.empty()) {
                    auto* q = metadata(p.quality);
                    require(
                        MmsValue_getBitStringSize(q) >= 13 && MmsValue_getBitStringSize(q) <= 16,
                        "invalid_quality_bit_string_size");
                    const auto quality = MmsValue_getBitStringAsInteger(q);
                    item["iec_quality"] = quality;
                    item["quality_source"] = "device";
                    good_quality = (quality & 0x1fff) == 0;
                }
                if (!p.timestamp.empty()) {
                    auto* timestamp = metadata(p.timestamp);
                    const auto time_quality = MmsValue_getUtcTimeQuality(timestamp);
                    item["timestamp"] = MmsValue_getUtcTimeInMs(timestamp);
                    item["timestamp_source"] = "device";
                    item["time_quality"] = time_quality;
                    good_time = (time_quality & 0x60) == 0;
                }
                item["metadata_coherent"] = coherent;
                require(!config.stale || Clock::now() - sample.arrival <=
                                             std::chrono::milliseconds(config.stale),
                        "stale_value");
                require(good_quality, "invalid_device_quality");
                require(good_time, "invalid_device_clock");
                require(!p.invalid.count(decoded), "invalid_state_value");
                item["value"] = decoded;
                item["quality"] = 0;
                ++valid;
            } catch (const std::exception& error) {
                item["error"] = error.what();
            }
            bytes += item.dump().size() + 1;
            require(bytes <= static_cast<std::size_t>(enum_value(PluginLimits::MaxResultBytes)),
                    "result size exceeds 8 MiB; split the batch");
            result.push_back(std::move(item));
        }
        if (!report_warning.empty())
            for (auto& item : result) item["report_warning"] = report_warning;
        output = result.dump();
        require(
            output.size() + 1 <= static_cast<std::size_t>(enum_value(PluginLimits::MaxResultBytes)),
            "result size exceeds 8 MiB");
        error.clear();
        return valid;
    } catch (const std::exception& error) {
        output.clear();
        this->error = error.what();
        return -1;
    }
}
}  // namespace ems::net::protocol
