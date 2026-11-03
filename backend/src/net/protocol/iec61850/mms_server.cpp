#include <iec61850_dynamic_model.h>
#include <iec61850_server.h>
#include <mms_server.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

#include "mms_server_runtime.h"
#include "net/protocol/protocol_types.h"

namespace ems::net::protocol {
namespace {
using Json = nlohmann::json;

void check(bool condition, const std::string& error) {
    if (!condition) throw std::invalid_argument(error);
}

void fields(const Json& object, const std::set<std::string>& names) {
    check(object.is_object(), "server configuration must be an object");
    for (auto it = object.begin(); it != object.end(); ++it)
        check(names.count(it.key()) != 0, "unsupported server field: " + it.key());
}

int setting(const Json& object, const char* name, int fallback, int minimum, int maximum) {
    if (!object.contains(name)) return fallback;
    check(object.at(name).is_number_integer(), std::string(name) + " must be an integer");
    const auto value = object.at(name).get<std::int64_t>();
    check(value >= minimum && value <= maximum, std::string(name) + " out of range");
    return static_cast<int>(value);
}

std::uint64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool name_valid(const std::string& text) {
    return !text.empty() &&
           text.find_first_not_of(
               "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") ==
               std::string::npos;
}

std::vector<std::string> parts(const std::string& text, char separator) {
    std::vector<std::string> result;
    std::size_t start = 0;
    do {
        const auto end = text.find(separator, start);
        result.push_back(text.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) return result;
        start = end + 1;
    } while (true);
}

struct Ref {
    std::string ld;
    std::vector<std::string> path;
};

Ref reference(const std::string& text, std::size_t minimum) {
    const auto slash = text.find('/');
    check(slash != std::string::npos && slash <= 64 && name_valid(text.substr(0, slash)),
          "invalid server LD reference: " + text);
    Ref ref{text.substr(0, slash), parts(text.substr(slash + 1), '.')};
    check(ref.path.size() >= minimum && text.size() - slash - 1 + 3 <= 64,
          "invalid server object reference: " + text);
    for (const auto& name : ref.path) check(name_valid(name), "invalid server node: " + name);
    return ref;
}

std::string ds_ref(std::string text) {
    std::replace(text.begin(), text.end(), '$', '.');
    const auto parsed = reference(text, 2);
    check(parsed.path.size() == 2, "invalid server DataSet reference");
    return parsed.ld + "/" + parsed.path[0] + "$" + parsed.path[1];
}

FunctionalConstraint fc(const std::string& name) {
    check(std::set<std::string>{"MX", "ST", "SP", "CF", "DC", "EX"}.count(name) != 0,
          "unsupported server FC: " + name);
    return FunctionalConstraint_fromString(name.c_str());
}

struct Type {
    DataAttributeType native;
    MmsType mms;
};

Type type(const std::string& name) {
    static const std::map<std::string, Type> types{{"boolean", {IEC61850_BOOLEAN, MMS_BOOLEAN}},
                                                   {"integer", {IEC61850_INT32, MMS_INTEGER}},
                                                   {"unsigned", {IEC61850_INT32U, MMS_UNSIGNED}},
                                                   {"float32", {IEC61850_FLOAT32, MMS_FLOAT}},
                                                   {"float64", {IEC61850_FLOAT64, MMS_FLOAT}}};
    const auto found = types.find(name);
    check(found != types.end(), "server point requires explicit mms_type");
    return found->second;
}

double numeric(const Json& json) {
    check(json.is_number() || json.is_boolean(), "server value must be numeric");
    const double value = json.is_boolean() ? (json.get<bool>() ? 1.0 : 0.0) : json.get<double>();
    check(std::isfinite(value), "server value must be finite");
    return value;
}

void validate_value(const std::string& name, double value) {
    check(std::isfinite(value), "non_finite_value");
    if (name == "boolean") check(value == 0 || value == 1, "boolean requires 0 or 1");
    if (name == "integer")
        check(std::trunc(value) == value && value >= INT32_MIN && value <= INT32_MAX,
              "integer value out of range");
    if (name == "unsigned")
        check(std::trunc(value) == value && value >= 0 && value <= UINT32_MAX,
              "unsigned value out of range");
    if (name == "float32")
        check(std::abs(value) <= std::numeric_limits<float>::max(), "float32 value out of range");
}

struct Lock {
    IedServer server;

    explicit Lock(IedServer value) : server(value) { IedServer_lockDataModel(server); }

    ~Lock() { IedServer_unlockDataModel(server); }
};
}  // namespace

class MmsReadOnlyServer::Impl {
   public:
    struct Point {
        std::string code, ref, name, quality_ref, timestamp_ref, quality_code;
        FunctionalConstraint constraint;
        DataAttribute *value{nullptr}, *quality{nullptr}, *timestamp{nullptr};
        double initial{0};
        Quality initial_quality{QUALITY_VALIDITY_INVALID};
        std::uint64_t initial_time{0};
        std::set<double> invalid;
    };

    IedModel* model{nullptr};
    IedServer server{nullptr};
    std::string host, error;
    int port, max_points{65536}, max_nodes{100000}, max_depth{32};
    std::map<std::string, LogicalDevice*> devices;
    std::map<std::string, LogicalNode*> nodes;
    std::map<std::string, DataAttribute*> attributes;
    std::map<std::string, Point> points;
    std::map<std::string, std::string> quality_codes;
    std::size_t node_count{0};

    Impl(const std::string& text, std::string address, int number)
        : host(std::move(address)), port(number) {
        try {
            build(Json::parse(text));
        } catch (...) {
            release();
            throw;
        }
    }

    ~Impl() { release(); }

    void release() {
        if (server) {
            IedServer_stop(server);
            IedServer_destroy(server);
            server = nullptr;
        }
        if (model) {
            IedModel_destroy(model);
            model = nullptr;
        }
    }

    void count(std::size_t depth) {
        check(++node_count <= static_cast<std::size_t>(max_nodes) &&
                  depth <= static_cast<std::size_t>(max_depth),
              "server model resource limit exceeded");
    }

    LogicalNode* ln(const Ref& ref) {
        auto device = devices.find(ref.ld);
        if (device == devices.end()) {
            count(1);
            auto* created = LogicalDevice_createEx(ref.ld.c_str(), model, ref.ld.c_str());
            check(created, "cannot create logical device");
            device = devices.emplace(ref.ld, created).first;
        }
        const auto key = ref.ld + "/" + ref.path[0];
        auto found = nodes.find(key);
        if (found == nodes.end()) {
            count(2);
            auto* created = LogicalNode_create(ref.path[0].c_str(), device->second);
            check(created, "cannot create logical node");
            found = nodes.emplace(key, created).first;
        }
        return found->second;
    }

    DataAttribute* attribute(const std::string& text, FunctionalConstraint constraint,
                             DataAttributeType native) {
        const auto parsed = reference(text, 3);
        auto* parent = reinterpret_cast<ModelNode*>(ln(parsed));
        for (std::size_t i = 1; i < parsed.path.size(); ++i) {
            auto* child =
                i == 1 ? ModelNode_getChild(parent, parsed.path[i].c_str())
                       : ModelNode_getChildWithFc(parent, parsed.path[i].c_str(), constraint);
            const bool leaf = i + 1 == parsed.path.size();
            if (!child) {
                count(i + 2);
                if (i == 1)
                    child = reinterpret_cast<ModelNode*>(
                        DataObject_create(parsed.path[i].c_str(), parent, 0));
                else
                    child = reinterpret_cast<ModelNode*>(DataAttribute_create(
                        parsed.path[i].c_str(), parent, leaf ? native : IEC61850_CONSTRUCTED,
                        constraint,
                        TRG_OPT_DATA_CHANGED | TRG_OPT_DATA_UPDATE | TRG_OPT_QUALITY_CHANGED, 0,
                        0));
                check(child, "cannot create model node");
            }
            if (i == 1)
                check(child->modelType == DataObjectModelType, "model path conflict");
            else {
                check(child->modelType == DataAttributeModelType, "model path conflict");
                const auto* attr = reinterpret_cast<DataAttribute*>(child);
                check(attr->type == (leaf ? native : IEC61850_CONSTRUCTED),
                      "model attribute type conflict: " + text);
            }
            parent = child;
        }
        auto* result = reinterpret_cast<DataAttribute*>(parent);
        attributes[std::string(FunctionalConstraint_toString(constraint)) + ":" + text] = result;
        return result;
    }

    void update(const Point& point, double value, Quality quality, std::uint64_t timestamp) {
        if (point.name == "boolean")
            IedServer_updateBooleanAttributeValue(server, point.value, value != 0);
        else if (point.name == "integer")
            IedServer_updateInt32AttributeValue(server, point.value,
                                                static_cast<std::int32_t>(value));
        else if (point.name == "unsigned")
            IedServer_updateUnsignedAttributeValue(server, point.value,
                                                   static_cast<std::uint32_t>(value));
        else if (point.name == "float32")
            IedServer_updateFloatAttributeValue(server, point.value, static_cast<float>(value));
        else {
            std::unique_ptr<MmsValue, decltype(&MmsValue_delete)> data(MmsValue_newDouble(value),
                                                                       MmsValue_delete);
            check(data != nullptr, "cannot allocate double value");
            IedServer_updateAttributeValue(server, point.value, data.get());
        }
        if (point.quality) IedServer_updateQuality(server, point.quality, quality);
        if (point.timestamp)
            IedServer_updateUTCTimeAttributeValue(server, point.timestamp, timestamp);
    }

    void build(const Json& options) {
        fields(options, {"ied_name", "points", "datasets", "reports", "max_points",
                         "max_model_nodes", "max_model_depth", "max_connections",
                         "report_buffer_bytes", "allow_writes", "read_mode"});
        check(!options.value("allow_writes", false), "server remote writes are not supported");
        check(options.value("read_mode", std::string("poll")) == "poll",
              "server read_mode must be poll");
        max_points = setting(options, "max_points", 65536, 1, 65536);
        max_nodes = setting(options, "max_model_nodes", 100000, 1, 1000000);
        max_depth = setting(options, "max_model_depth", 32, 1, 64);
        const auto ied = options.value("ied_name", std::string("EMS"));
        check(name_valid(ied) && ied.size() <= 64, "invalid ied_name");
        model = IedModel_create(ied.c_str());
        check(model, "cannot create server model");
        const auto& configured = options.at("points");
        check(configured.is_array() && configured.size() <= static_cast<std::size_t>(max_points),
              "invalid server points");
        std::map<std::string, Json> physical;
        std::map<std::string, std::string> metadata_owners;
        for (const auto& item : configured) {
            fields(item, {"code", "object_ref", "fc", "mms_type", "quality_ref", "timestamp_ref",
                          "quality_code", "value", "iec_quality", "timestamp", "invalid_values"});
            Point point;
            point.code = item.at("code").get<std::string>();
            check(!point.code.empty() && point.code.size() <= 256 &&
                      point.code.find('\0') == std::string::npos && !points.count(point.code),
                  "invalid or duplicate server code");
            point.ref = item.at("object_ref").get<std::string>();
            point.constraint = fc(item.at("fc").get<std::string>());
            point.name = item.at("mms_type").get<std::string>();
            point.value = attribute(point.ref, point.constraint, type(point.name).native);
            point.quality_ref = item.value("quality_ref", std::string());
            point.timestamp_ref = item.value("timestamp_ref", std::string());
            if (!point.quality_ref.empty())
                point.quality = attribute(point.quality_ref, point.constraint, IEC61850_QUALITY);
            if (!point.timestamp_ref.empty())
                point.timestamp =
                    attribute(point.timestamp_ref, point.constraint, IEC61850_TIMESTAMP);
            point.quality_code = item.value("quality_code", std::string());
            check(point.quality_code.empty() ||
                      (point.quality && point.quality_code.size() <= 256 &&
                       point.quality_code.find('\0') == std::string::npos &&
                       quality_codes.emplace(point.quality_code, point.code).second),
                  "invalid quality_code");
            check(item.contains("value") || point.quality,
                  "uninitialized server point requires quality_ref");
            if (item.contains("value")) point.initial = numeric(item.at("value"));
            validate_value(point.name, point.initial);
            point.initial_quality = static_cast<Quality>(
                setting(item, "iec_quality", item.contains("value") ? 0 : QUALITY_VALIDITY_INVALID,
                        0, 0x1fff));
            check(item.contains("value") || (point.initial_quality & 0x1fff) != 0,
                  "uninitialized server point must have invalid quality");
            point.initial_time = now_ms();
            if (item.contains("timestamp")) {
                const auto number = numeric(item.at("timestamp"));
                check(number >= 0 && number <= 9007199254740991.0 && std::trunc(number) == number,
                      "invalid timestamp");
                point.initial_time = static_cast<std::uint64_t>(number);
            }
            if (item.contains("invalid_values")) {
                check(
                    item.at("invalid_values").is_array() && item.at("invalid_values").size() <= 256,
                    "invalid invalid_values");
                for (const auto& invalid : item.at("invalid_values"))
                    point.invalid.insert(numeric(invalid));
            }
            const auto physical_key =
                std::string(FunctionalConstraint_toString(point.constraint)) + ":" + point.ref;
            auto definition = item;
            definition.erase("code");
            definition.erase("quality_code");
            const auto inserted = physical.emplace(physical_key, definition);
            check(inserted.second || inserted.first->second == definition,
                  "conflicting physical server point");
            for (const auto& metadata : {point.quality_ref, point.timestamp_ref}) {
                if (metadata.empty()) continue;
                const auto inserted = metadata_owners.emplace(
                    std::string(FunctionalConstraint_toString(point.constraint)) + ":" + metadata,
                    physical_key);
                check(inserted.second || inserted.first->second == physical_key,
                      "metadata cannot be shared by distinct generated value points");
            }
            points.emplace(point.code, std::move(point));
        }
        for (const auto& [code, _] : quality_codes)
            check(!points.count(code), "quality_code conflicts with point code");
        std::set<std::string> datasets;
        const auto configured_sets = options.value("datasets", Json::array());
        check(configured_sets.is_array() && configured_sets.size() <= 4096,
              "invalid server datasets");
        for (const auto& dataset : configured_sets) {
            fields(dataset, {"dataset_ref", "members"});
            const auto text = ds_ref(dataset.at("dataset_ref").get<std::string>());
            check(datasets.insert(text).second, "duplicate server DataSet");
            auto normalized = text;
            std::replace(normalized.begin(), normalized.end(), '$', '.');
            const auto parsed = reference(normalized, 2);
            count(3);
            auto* target = DataSet_create(parsed.path[1].c_str(), ln(parsed));
            check(target, "cannot create server DataSet");
            const auto& members = dataset.at("members");
            check(members.is_array() && !members.empty() &&
                      members.size() <= static_cast<std::size_t>(max_points),
                  "invalid DataSet members");
            for (const auto& member : members) {
                count(4);
                fields(member, {"object_ref", "fc"});
                const auto text = member.at("object_ref").get<std::string>();
                const auto parsed_member = reference(text, 2);
                const auto constraint = fc(member.at("fc").get<std::string>());
                auto* node = IedModel_getModelNodeByObjectReference(model, text.c_str());
                check(node, "DataSet member absent from fixed model: " + text);
                if (node->modelType == DataAttributeModelType)
                    check(reinterpret_cast<DataAttribute*>(node)->fc == constraint,
                          "DataSet member FC mismatch");
                else {
                    const auto prefix =
                        std::string(FunctionalConstraint_toString(constraint)) + ":" + text + ".";
                    const auto found = attributes.lower_bound(prefix);
                    check(found != attributes.end() &&
                              found->first.compare(0, prefix.size(), prefix) == 0,
                          "DataSet FCD has no matching FC");
                }
                std::string variable = parsed_member.ld + "/" + parsed_member.path[0] + "$" +
                                       FunctionalConstraint_toString(constraint);
                for (std::size_t i = 1; i < parsed_member.path.size(); ++i)
                    variable += "$" + parsed_member.path[i];
                check(DataSetEntry_create(target, variable.c_str(), -1, nullptr),
                      "cannot create DataSet member");
            }
        }
        const auto reports = options.value("reports", Json::array());
        check(reports.is_array() && reports.size() <= 128, "invalid server reports");
        std::set<std::string> report_refs, report_ids;
        for (const auto& report : reports) {
            fields(report, {"rcb_ref", "dataset_ref", "rpt_id", "conf_rev", "buffer_time_ms",
                            "integrity_period_ms", "trigger_options"});
            const auto text = report.at("rcb_ref").get<std::string>();
            const auto parsed = reference(text, 3);
            check(parsed.path.size() == 3 && (parsed.path[1] == "BR" || parsed.path[1] == "RP") &&
                      report_refs.insert(text).second,
                  "invalid or duplicate server RCB");
            const auto dataset = ds_ref(report.at("dataset_ref").get<std::string>());
            const auto prefix = parsed.ld + "/" + parsed.path[0] + "$";
            check(datasets.count(dataset) && dataset.compare(0, prefix.size(), prefix) == 0,
                  "RCB DataSet must be configured in the same LN");
            const auto id = report.value("rpt_id", text);
            check(!id.empty() && id.size() <= 129 && id.find('\0') == std::string::npos &&
                      report_ids.insert(id).second,
                  "invalid or duplicate RptId");
            int triggers = 0;
            const auto trigger_names = report.value(
                "trigger_options", Json::array({"dchg", "qchg", "dupd", "integrity", "gi"}));
            check(trigger_names.is_array(), "invalid trigger_options");
            const std::map<std::string, int> trigger_bits{{"dchg", TRG_OPT_DATA_CHANGED},
                                                          {"qchg", TRG_OPT_QUALITY_CHANGED},
                                                          {"dupd", TRG_OPT_DATA_UPDATE},
                                                          {"integrity", TRG_OPT_INTEGRITY},
                                                          {"gi", TRG_OPT_GI}};
            for (const auto& name : trigger_names) {
                const auto found = trigger_bits.find(name.get<std::string>());
                check(found != trigger_bits.end(), "invalid trigger option");
                triggers |= found->second;
            }
            const int optional =
                RPT_OPT_SEQ_NUM | RPT_OPT_TIME_STAMP | RPT_OPT_REASON_FOR_INCLUSION |
                RPT_OPT_DATA_SET | RPT_OPT_CONF_REV |
                (parsed.path[1] == "BR" ? RPT_OPT_BUFFER_OVERFLOW | RPT_OPT_ENTRY_ID : 0);
            count(3);
            check(ReportControlBlock_create(
                      parsed.path[2].c_str(), ln(parsed), id.c_str(), parsed.path[1] == "BR",
                      dataset.substr(prefix.size()).c_str(),
                      setting(report, "conf_rev", 1, 1, INT32_MAX), triggers, optional,
                      setting(report, "buffer_time_ms", 0, 0, 60000),
                      setting(report, "integrity_period_ms", 10000, 1000, 86400000)),
                  "cannot create RCB");
        }
        std::unique_ptr<sIedServerConfig, decltype(&IedServerConfig_destroy)> config(
            IedServerConfig_create(), IedServerConfig_destroy);
        check(config != nullptr, "cannot allocate server config");
        IedServerConfig_setMaxMmsConnections(config.get(),
                                             setting(options, "max_connections", 8, 1, 100));
        const int buffer = setting(options, "report_buffer_bytes", 65536, 4096, 8388608);
        check(static_cast<std::uint64_t>(buffer) * reports.size() <= 64ULL * 1024 * 1024,
              "aggregate report buffers exceed 64 MiB");
        IedServerConfig_setReportBufferSize(config.get(), buffer);
        IedServerConfig_setReportBufferSizeForURCBs(config.get(), buffer);
        IedServerConfig_enableFileService(config.get(), false);
        IedServerConfig_enableDynamicDataSetService(config.get(), false);
        IedServerConfig_enableLogService(config.get(), false);
        IedServerConfig_enableEditSG(config.get(), false);
        IedServerConfig_useIntegratedGoosePublisher(config.get(), false);
        IedServerConfig_enableOwnerForRCB(config.get(), true);
        IedServerConfig_setReportSetting(config.get(), IEC61850_REPORTSETTINGS_RPT_ID, false);
        IedServerConfig_setReportSetting(config.get(), IEC61850_REPORTSETTINGS_DATSET, false);
        server = IedServer_createWithConfig(model, nullptr, config.get());
        check(server, "cannot allocate MMS server");
        IedServer_setLocalIpAddress(server, host.c_str());
        for (const auto constraint :
             {IEC61850_FC_ST, IEC61850_FC_MX, IEC61850_FC_SP, IEC61850_FC_SV, IEC61850_FC_CF,
              IEC61850_FC_DC, IEC61850_FC_SG, IEC61850_FC_SE, IEC61850_FC_CO})
            IedServer_setWriteAccessPolicy(server, constraint, ACCESS_POLICY_DENY);
        IedServer_handleWriteAccessGlobally(
            server,
            [](DataAttribute*, MmsValue*, ClientConnection, void*) -> MmsDataAccessError {
                return DATA_ACCESS_ERROR_OBJECT_ACCESS_DENIED;
            },
            nullptr);
        Lock lock(server);
        for (const auto& [_, point] : points)
            update(point, point.initial,
                   point.initial_quality |
                       (point.invalid.count(point.initial) ? QUALITY_VALIDITY_INVALID : 0),
                   point.initial_time);
    }
};

MmsReadOnlyServer::MmsReadOnlyServer(const std::string& options, const std::string& host, int port)
    : impl_(std::make_unique<Impl>(options, host, port)) {}

MmsReadOnlyServer::~MmsReadOnlyServer() = default;

int MmsReadOnlyServer::connect() {
    if (is_connected()) return 0;
    IedServer_start(impl_->server, impl_->port);
    if (!MmsServer_isRunning(IedServer_getMmsServer(impl_->server))) {
        IedServer_stop(impl_->server);
        impl_->error = "MMS server listen failed";
        return -1;
    }
    impl_->error.clear();
    return 0;
}

int MmsReadOnlyServer::disconnect() {
    IedServer_stop(impl_->server);
    impl_->error.clear();
    return 0;
}

bool MmsReadOnlyServer::is_connected() const {
    return MmsServer_isRunning(IedServer_getMmsServer(impl_->server));
}

std::string MmsReadOnlyServer::last_error() const { return impl_->error; }

int MmsReadOnlyServer::write(const std::string& code, double value) {
    try {
        auto quality = impl_->quality_codes.find(code);
        if (quality != impl_->quality_codes.end()) {
            check(
                std::isfinite(value) && std::trunc(value) == value && value >= 0 && value <= 0x1fff,
                "invalid IEC quality mask");
            const auto& point = impl_->points.at(quality->second);
            Lock lock(impl_->server);
            IedServer_updateQuality(impl_->server, point.quality, static_cast<Quality>(value));
            if (point.timestamp)
                IedServer_updateUTCTimeAttributeValue(impl_->server, point.timestamp, now_ms());
        } else {
            const auto found = impl_->points.find(code);
            check(found != impl_->points.end(), "point_not_configured");
            const auto& point = found->second;
            validate_value(point.name, value);
            Lock lock(impl_->server);
            impl_->update(
                point, value,
                point.invalid.count(value) ? QUALITY_VALIDITY_INVALID : QUALITY_VALIDITY_GOOD,
                now_ms());
        }
        impl_->error.clear();
        return 0;
    } catch (const std::exception& error) {
        impl_->error = error.what();
        return -1;
    }
}

int MmsReadOnlyServer::read_batch(const std::vector<std::string>& codes, std::string& output) {
    try {
        check(codes.size() <= static_cast<std::size_t>(impl_->max_points),
              "batch point limit exceeded");
        Json result = Json::array();
        int valid = 0;
        std::size_t bytes = 2;
        Lock lock(impl_->server);
        for (const auto& code : codes) {
            Json item{{"code", code},
                      {"value", nullptr},
                      {"quality", 1},
                      {"timestamp", now_ms()},
                      {"source", "local_image"},
                      {"timestamp_source", "local_image"}};
            try {
                check(code.size() <= 256, "point code exceeds 256 bytes");
                const auto quality_alias = impl_->quality_codes.find(code);
                const bool read_quality = quality_alias != impl_->quality_codes.end();
                const auto found = impl_->points.find(read_quality ? quality_alias->second : code);
                check(found != impl_->points.end(), "point_not_configured");
                const auto& point = found->second;
                auto* raw = IedServer_getAttributeValue(impl_->server, point.value);
                double value;
                if (read_quality)
                    value = MmsValue_getBitStringAsInteger(
                        IedServer_getAttributeValue(impl_->server, point.quality));
                else if (point.name == "boolean")
                    value = MmsValue_getBoolean(raw) ? 1 : 0;
                else if (point.name == "integer")
                    value = MmsValue_toInt32(raw);
                else if (point.name == "unsigned")
                    value = MmsValue_toUint32(raw);
                else
                    value = MmsValue_toDouble(raw);
                item["object_ref"] = read_quality ? point.quality_ref : point.ref;
                item["fc"] = FunctionalConstraint_toString(point.constraint);
                item["mms_type"] = read_quality ? "quality" : point.name;
                if (point.timestamp)
                    item["timestamp"] = MmsValue_getUtcTimeInMs(
                        IedServer_getAttributeValue(impl_->server, point.timestamp));
                const auto quality =
                    point.quality ? MmsValue_getBitStringAsInteger(
                                        IedServer_getAttributeValue(impl_->server, point.quality))
                                  : 0;
                if (point.quality) item["iec_quality"] = quality;
                item["quality_source"] = point.quality ? "local_image" : "unavailable";
                check(read_quality || ((quality & 0x1fff) == 0 && !point.invalid.count(value)),
                      "invalid_device_quality");
                item["value"] = value;
                item["quality"] = 0;
                ++valid;
            } catch (const std::exception& error) {
                item["error"] = error.what();
            }
            bytes += item.dump().size() + 1;
            check(bytes <= static_cast<std::size_t>(enum_value(PluginLimits::MaxResultBytes)),
                  "result size exceeds 8 MiB");
            result.push_back(std::move(item));
        }
        output = result.dump();
        impl_->error.clear();
        return valid;
    } catch (const std::exception& error) {
        output.clear();
        impl_->error = error.what();
        return -1;
    }
}
}  // namespace ems::net::protocol
