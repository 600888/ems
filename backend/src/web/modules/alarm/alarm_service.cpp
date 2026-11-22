#include "alarm_service.h"

#include <common/core/time.h>
#include <common/database/timeseries/factory.h>
#include <rtdb/client/point_client.h>
#include <rtdb/model/configuration_catalog.h>

#include <algorithm>
#include <array>
#include <map>
#include <unordered_map>

#include "web/modules/identity/application/authorization_service.h"
#include "web/platform/crypto.h"

namespace ems::web {
namespace {
using namespace database;
constexpr auto kMaxTimeMs = kMaxAlarmArchiveTimeUs / 1000;

[[noreturn]] void invalid(const char* reason) { throw ApiError(400, "InvalidArgument", reason); }

[[noreturn]] void corrupt() {
    throw ApiError(503, "CorruptData", "invalid_alarm_data", "NotApplicable");
}

template <class T>
T alarm_value(core::Result<T> value, const char* unavailable, bool writing = false) {
    if (!core::is_ok(value)) {
        const auto& error = core::error_of(value);
        const auto* effect = writing ? core::to_string(error.effect) : "NotApplicable";
        if (error.code == core::ErrorCode::ResourceExhausted)
            throw ApiError(503, "ResourceExhausted", "alarm_query_budget", effect);
        if (error.code == core::ErrorCode::DeadlineExceeded)
            throw ApiError(504, "DeadlineExceeded", "deadline_exceeded", effect);
        if (error.code == core::ErrorCode::Conflict && writing) fail(error);
        throw ApiError(503, "Unavailable", unavailable, effect);
    }
    return std::move(std::get<0>(value));
}

template <class T>
T from_repository(core::Result<T> value, bool writing = false) {
    return alarm_value(std::move(value), "alarm_service_unavailable", writing);
}

std::int64_t integer(const Json& body, const char* key, std::int64_t max) {
    const auto it = body.find(key);
    if (it == body.end() || !it->is_number_integer() ||
        (it->is_number_unsigned() ? it->get<std::uint64_t>() > static_cast<std::uint64_t>(max)
                                  : it->get<std::int64_t>() < 0 || it->get<std::int64_t>() > max))
        invalid("alarm_bounds");
    return it->get<std::int64_t>();
}

std::string optional_text(const Json& body, const char* key, std::size_t max) {
    const auto it = body.find(key);
    if (it == body.end()) return {};
    if (!it->is_string()) invalid("alarm_filter");
    const auto& value = it->get_ref<const std::string&>();
    if (value.size() > max || value.find('\0') != value.npos) invalid("alarm_filter");
    return value;
}

std::string lower_ascii(std::string text) {
    for (auto& ch : text)
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    return text;
}

struct Filters {
    bool history{false};
    std::string root, severity, acknowledgement, recovery, keyword;
    core::UtcMicros start_us{0}, end_us{0};
    std::size_t offset{0}, limit{0};
};

Filters parse_filters(const Json& body, bool exporting) {
    if (exporting)
        fields(body, {"view", "device_root_id", "severity", "acknowledgement", "recovery",
                      "keyword", "start_ms", "end_ms", "include_descendants", "max_rows"});
    else
        fields(body, {"view", "device_root_id", "severity", "acknowledgement", "recovery",
                      "keyword", "start_ms", "end_ms", "include_descendants", "page", "page_size"});
    const auto view = text(body, "view", 7);
    if (view != "active" && view != "history") invalid("alarm_filter");
    if (body.contains("include_descendants") && body.at("include_descendants") != Json(true))
        invalid("alarm_filter");
    Filters filter;
    filter.history = view == "history";
    filter.root = optional_text(body, "device_root_id", 64);
    filter.severity = optional_text(body, "severity", 12);
    filter.acknowledgement = optional_text(body, "acknowledgement", 16);
    filter.recovery = optional_text(body, "recovery", 12);
    filter.keyword = lower_ascii(optional_text(body, "keyword", 512));
    if ((!filter.severity.empty() && filter.severity != "critical" &&
         filter.severity != "warning" && filter.severity != "info") ||
        (!filter.acknowledgement.empty() && filter.acknowledgement != "pending" &&
         filter.acknowledgement != "acknowledged") ||
        (!filter.recovery.empty() && filter.recovery != "active" && filter.recovery != "cleared"))
        invalid("alarm_filter");
    if (filter.history) {
        const auto start = integer(body, "start_ms", kMaxTimeMs);
        const auto end = integer(body, "end_ms", kMaxTimeMs);
        if (end < start || end - start > 31LL * 86400000) invalid("alarm_bounds");
        filter.start_us = start * 1000;
        filter.end_us = std::min(end * 1000 + 999, kMaxAlarmArchiveTimeUs);
    }
    if (exporting) {
        filter.limit = static_cast<std::size_t>(integer(body, "max_rows", 10000));
        if (!filter.limit) invalid("alarm_bounds");
    } else {
        const auto page = integer(body, "page", 100000);
        filter.limit = static_cast<std::size_t>(integer(body, "page_size", 50));
        if (!page || (filter.limit != 8 && filter.limit != 20 && filter.limit != 50))
            invalid("alarm_bounds");
        filter.offset = static_cast<std::size_t>(page - 1) * filter.limit;
    }
    return filter;
}

std::string remark(const Json& body) {
    const auto raw = text(body, "remark", 8192);
    static constexpr std::array<std::string_view, 25> whitespace = {
        " ",      "\t",     "\n",     "\r",     "\v",     "\f",     "\u00a0", "\u1680", "\u2000",
        "\u2001", "\u2002", "\u2003", "\u2004", "\u2005", "\u2006", "\u2007", "\u2008", "\u2009",
        "\u200a", "\u2028", "\u2029", "\u202f", "\u205f", "\u3000", "\ufeff"};
    std::size_t start = raw.size(), end = 0, index = 0, first = 0, last = 0;
    for (std::size_t i = 0; i < raw.size(); ++index) {
        auto next = i + 1;
        while (next < raw.size() && (static_cast<unsigned char>(raw[next]) & 0xc0) == 0x80) ++next;
        const std::string_view codepoint(raw.data() + i, next - i);
        if (std::find(whitespace.begin(), whitespace.end(), codepoint) == whitespace.end()) {
            if (start == raw.size()) {
                start = i;
                first = index;
            }
            last = index;
            end = next;
        }
        i = next;
    }
    if (start == raw.size() || last - first >= 500 || raw.find('\0') != raw.npos)
        invalid("alarm_remark");
    return raw.substr(start, end - start);
}

// 授权根据当前站根和真实父链计算。导航祖先只用于选择和汇总。
class DeviceScope {
   public:
    explicit DeviceScope(DeviceRequest& request)
        : station_(request.station.at("device_id")), roots_(request.principal.device_roots) {
        DeviceQuery query;
        query.table = DeviceTable::Device;
        query.limit = 1000;
        for (;; query.offset += query.limit) {
            const auto rows = request.rows(query);
            for (const auto& row : rows)
                parents_.emplace(row.at("device_id"), row.at("parent_device_id"));
            if (rows.size() < query.limit) break;
        }
    }

    std::vector<std::string> authorized_path(const std::string& id) const {
        std::vector<std::string> path;
        bool allowed = roots_.count("*") != 0;
        auto node = id;
        for (int depth = 0; depth <= 8; ++depth) {
            const auto parent = parents_.find(node);
            if (parent == parents_.end()) return {};
            path.push_back(node);
            allowed = allowed || roots_.count(node) != 0;
            if (node == station_) return allowed ? path : std::vector<std::string>{};
            node = parent->second;
        }
        return {};
    }

   private:
    std::string station_;
    std::set<std::string> roots_;
    std::unordered_map<std::string, std::string> parents_;
};

Json object(const std::string& payload, const core::StationId& station, const model::AlarmId& id,
            const core::DeviceId& device) {
    auto row = Json::parse(payload, nullptr, false);
    if (!row.is_object() || row.value("station_id", Json()) != station.value() ||
        row.value("alarm_id", Json()) != id.value() ||
        row.value("device_id", Json()) != device.value())
        corrupt();
    return row;
}

std::int64_t millis(const Json& value) {
    if (!value.is_number_integer() ||
        (value.is_number_unsigned()
             ? value.get<std::uint64_t>() > static_cast<std::uint64_t>(kMaxAlarmArchiveTimeUs)
             : value.get<std::int64_t>() < 0 || value.get<std::int64_t>() > kMaxAlarmArchiveTimeUs))
        corrupt();
    return value.get<std::int64_t>() / 1000;
}

Json record(const Json& row) {
    try {
        Json result = Json::object();
        for (auto key : {"alarm_id", "alarm_code", "name", "device_id", "device_name",
                         "device_path", "trigger_summary"})
            result[key] = row.at(key).get<std::string>();
        auto severity = lower_ascii(row.at("severity").get<std::string>());
        if (severity != "critical" && severity != "warning" && severity != "info") corrupt();
        result["severity"] = severity;
        result["occurred_at_ms"] = millis(row.at("occurred_at_us"));
        const auto& cleared = row.at("cleared_at_us");
        result["cleared_at_ms"] = cleared.is_null() ? Json() : Json(millis(cleared));
        const auto& ack = row.at("acknowledgement");
        result["acknowledgement"] = nullptr;
        if (!ack.is_null())
            result["acknowledgement"] = {
                {"user_id", ack.at("user_id").get<std::string>()},
                {"display_name", ack.at("display_name").get<std::string>()},
                {"remark", ack.at("remark").get<std::string>()},
                {"at_ms", millis(ack.at("at_us"))}};
        return result;
    } catch (const Json::exception&) {
        corrupt();
    }
}

bool matches(const Json& item, const Filters& filter) {
    const bool active = item.at("cleared_at_ms").is_null();
    const bool pending = item.at("acknowledgement").is_null();
    if ((!filter.history && !active) ||
        (!filter.severity.empty() && item.at("severity") != filter.severity) ||
        (!filter.recovery.empty() && (filter.recovery == "active") != active) ||
        (!filter.acknowledgement.empty() && (filter.acknowledgement == "pending") != pending))
        return false;
    if (filter.keyword.empty()) return true;
    for (auto key : {"name", "alarm_code", "device_name", "device_path"})
        if (lower_ascii(item.at(key).get<std::string>()).find(filter.keyword) != std::string::npos)
            return true;
    return false;
}

void increment(Json& counters, const std::string& key) {
    counters[key] = counters.value(key, std::size_t{0}) + 1;
}

Json archive_meta(const AlarmArchiveProgress& progress, const DeviceRequest& request) {
    const auto& roots = request.principal.device_roots;
    if (!roots.count("*") && !roots.count(request.station.at("device_id"))) return Json::object();
    return {{"alarm_archive",
             {{"archived_seq", std::to_string(progress.archived_seq)},
              {"committed_seq", std::to_string(progress.committed_seq)},
              {"pending", progress.committed_seq - progress.archived_seq}}}};
}

core::StationId station_id(const DeviceRequest& request) {
    return core::StationId(request.station.at("device_id").get<std::string>());
}

std::optional<AlarmState> find(DeviceRequest& request, IAlarmRepository& repo,
                               const model::AlarmId& id) {
    auto state =
        from_repository(repo.find(*request.transaction, station_id(request), id, request.context));
    if (!state) return std::nullopt;
    try {
        request.device(state->device_id.value());
    } catch (const ApiError& error) {
        if (error.status == 404) return std::nullopt;
        throw;
    }
    return state;
}

void missing() { throw ApiError(404, "NotFound", "alarm_not_found", "NotApplicable"); }

std::map<std::string, ArchivedAlarmRow> latest(std::vector<ArchivedAlarmRow> rows) {
    std::map<std::string, ArchivedAlarmRow> result;
    for (auto& row : rows) {
        const auto id = row.alarm_id.value();
        const auto it = result.find(id);
        if (it == result.end() || it->second.stream_seq < row.stream_seq)
            result.insert_or_assign(id, std::move(row));
    }
    return result;
}

Json lifecycle(std::vector<ArchivedAlarmRow>& rows, const core::StationId& station,
               const AlarmState& state, const Json& record) {
    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return a.stream_seq < b.stream_seq; });
    Json events = Json::array();
    std::set<std::string> seen, kinds;
    try {
        for (const auto& row : rows) {
            auto data = object(row.payload, station, state.alarm_id, state.device_id);
            const auto kind = lower_ascii(data.at("kind").get<std::string>());
            const auto id = data.at("event_id").get<std::string>();
            if (id.empty() || !seen.insert(id).second || !kinds.insert(kind).second) corrupt();
            const auto at = millis(data.at("event_time_us"));
            Json event = {{"event_id", id},
                          {"kind", kind},
                          {"at_ms", at},
                          {"detail", data.at("detail").get<std::string>()}};
            if (kind == "occurred" && at == record.at("occurred_at_ms")) {
                if (!events.empty()) corrupt();
            } else if (kind == "cleared" && at == record.at("cleared_at_ms")) {
                if (events.empty()) corrupt();
            } else if (kind == "acknowledged" && !record.at("acknowledgement").is_null() &&
                       at == record.at("acknowledgement").at("at_ms")) {
                if (events.empty()) corrupt();
                event["display_name"] =
                    data.at("acknowledgement").at("display_name").get<std::string>();
            } else {
                corrupt();
            }
            events.push_back(std::move(event));
        }
        if (!kinds.count("occurred") ||
            kinds.count("cleared") == record.at("cleared_at_ms").is_null() ||
            kinds.count("acknowledged") == record.at("acknowledgement").is_null())
            corrupt();
    } catch (const Json::exception&) {
        corrupt();
    }
    return events;
}

void current_value(Json& data, DeviceRequest& request, const std::string& config) {
    if (!data.contains("point_id")) return;
    data["quality"] = "missing";
    if (config.empty()) return;
    for (const auto table : {DeviceTable::Yx, DeviceTable::Yc}) {
        DeviceQuery q;
        q.table = table;
        q.filters = {{"point_id", data.at("point_id")}, {"owner_device_id", data.at("device_id")}};
        q.limit = 1;
        const auto points = request.rows(q);
        if (points.empty()) continue;
        const auto& point = points.front();
        const auto records = take(request.database.device_config->load_configuration(
            *request.transaction, request.context));
        const auto catalog = rtdb::configuration_catalog(records);
        if (!core::is_ok(catalog)) return;
        auto client = rtdb::PointClient::open(config, core::value_of(catalog), request.context);
        if (!core::is_ok(client)) return;
        auto value = core::value_of(client)->readPointJson(table == DeviceTable::Yx ? "yx" : "yc",
                                                           data.at("point_id"), request.context);
        if (!core::is_ok(value)) return;
        const auto sample = Json::parse(core::value_of(value), nullptr, false);
        if (!sample.is_object() || !sample.contains("timestamp") ||
            !sample.at("timestamp").is_number_integer() || !sample.contains("quality") ||
            !sample.contains("value"))
            return;
        const auto timestamp = sample.at("timestamp").get<std::int64_t>();
        const auto now = core::now_utc_micros() / 1000;
        const auto own_stale = point.value("stale_after_ms", 0);
        const auto stale =
            own_stale > 0 ? own_stale : request.station.value("default_stale_after_ms", 0);
        if (timestamp <= 0 || timestamp > now + 300000 || (stale > 0 && now - timestamp > stale))
            data["quality"] = "stale";
        else if (sample.at("quality") != 0 || sample.at("value").is_null())
            data["quality"] = "invalid";
        else {
            const auto& reading = sample.at("value");
            data["quality"] = "good";
            data["current_value"] =
                reading.is_string() ? reading.get<std::string>() : reading.dump();
            data["sampled_at_ms"] = timestamp;
        }
        return;
    }
}
}  // namespace

AlarmService::AlarmService(database::MySqlDatabaseContext database, std::string timeseries_config,
                           std::string realtime_config, OpenSeries open_series)
    : database_(std::move(database)),
      timeseries_config_(std::move(timeseries_config)),
      realtime_config_(std::move(realtime_config)),
      open_series_(std::move(open_series)) {}

DeviceResult AlarmService::execute(const std::string& path, const Json& body,
                                   const Principal& actor, const core::CallContext& ctx) {
    const bool ack = path == "/api/alarms/acknowledge";
    const auto* permission = ack ? "alarm.ack" : "alarm.read";
    actor.require(permission);
    if (!database_.alarms) throw ApiError(503, "NotReady", "alarm_service_unavailable");
    auto& repo = *database_.alarms;
    const auto open_series = [&] {
        const auto* unavailable = "alarm_archive_unavailable";
        auto series = alarm_value(
            open_series_ ? open_series_()
                         : connect_time_series(alarm_value(
                               load_time_series_options(timeseries_config_), unavailable)),
            unavailable);
        alarm_value(series->check_alarm_schema(ctx), unavailable);
        return series;
    };

    if (ack) {
        fields(body, {"alarm_ids", "remark"});
        const auto note = remark(body);
        const auto ids = body.find("alarm_ids");
        if (ids == body.end() || !ids->is_array() || ids->empty() || ids->size() > 200)
            invalid("alarm_ids");
        std::set<std::string> unique;
        for (const auto& id : *ids) {
            if (!id.is_string()) invalid("alarm_ids");
            const auto& value = id.get_ref<const std::string&>();
            if (value.empty() || value.size() > 64 || value.find('\0') != value.npos ||
                !unique.insert(value).second)
                invalid("alarm_ids");
        }
        Json items = Json::array();
        bool applied = false;
        for (const auto& id : *ids) {
            const auto alarm = model::AlarmId(id.get<std::string>());
            try {
                DeviceRequest request(database_, actor, ctx, true);
                request.principal.require(permission);
                const auto state = find(request, repo, alarm);
                if (!state) missing();
                std::string display_name = request.principal.subject_id;
                if (!request.principal.session_id.empty()) {
                    const auto user = identity_value(
                        database_.users->user(*request.transaction, request.principal.subject_id,
                                              false, IdentityLock::Share, ctx));
                    if (!user) throw ApiError(401, "Unauthenticated", "unauthenticated");
                    display_name = user->display_name;
                }
                const auto change = from_repository(
                    repo.apply(
                        *request.transaction,
                        {core::EventId(random_token()), alarm, state->station_id, state->device_id,
                         std::max(core::now_utc_micros(), state->occurred_at_us), note,
                         AlarmAcknowledgement{request.principal.subject_id, display_name, note}},
                        ctx),
                    true);
                request.finish({});
                applied = applied || change != AlarmChangeResult::AlreadyAcknowledged;
                items.push_back({{"alarm_id", id},
                                 {"status", change == AlarmChangeResult::AlreadyAcknowledged
                                                ? "already_acknowledged"
                                                : "acknowledged"}});
            } catch (const ApiError& error) {
                if (error.effect == "Unknown") throw;
                if (error.status == 401 || error.status >= 500 || error.status == 403) {
                    if (applied) throw ApiError(error.status, error.code, error.reason, "Unknown");
                    throw;
                }
                items.push_back(
                    {{"alarm_id", id},
                     {"status", "failed"},
                     {"message", error.status == 404 ? "告警不存在或无访问权限" : "告警确认冲突"}});
            }
        }
        return {{{"items", std::move(items)}}};
    }

    if (path == "/api/alarms/detail") {
        fields(body, {"alarm_id"});
        const auto id = model::AlarmId(text(body, "alarm_id"));
        core::StationId station;
        AlarmArchiveProgress progress;
        std::vector<AlarmArchiveItem> pending;
        std::optional<AlarmState> state;
        {
            DeviceRequest request(database_, actor, ctx, false);
            request.principal.require(permission);
            station = station_id(request);
            state = find(request, repo, id);
            if (!state) missing();
            progress = from_repository(repo.progress(*request.transaction, station, ctx));
            pending =
                from_repository(repo.pending(*request.transaction, station, progress, id, ctx));
            request.finish({});
        }
        auto series = open_series();
        AlarmArchiveQuery query{station, progress.archived_seq, id};
        auto snapshots = alarm_value(series->read_alarms(query, ctx), "alarm_archive_unavailable");
        query.events = true;
        auto events = alarm_value(series->read_alarms(query, ctx), "alarm_archive_unavailable");
        for (auto& item : pending) {
            snapshots.push_back(
                {item.alarm_id, item.device_id, item.stream_seq, std::move(item.state_json)});
            events.push_back(
                {item.alarm_id, item.device_id, item.stream_seq, std::move(item.event_json)});
        }
        if (snapshots.empty()) corrupt();
        const auto last = std::max_element(
            snapshots.begin(), snapshots.end(),
            [](const auto& a, const auto& b) { return a.stream_seq < b.stream_seq; });
        const auto row = object(last->payload, station, id, state->device_id);
        auto data = record(row);
        data["events"] = lifecycle(events, station, *state, data);
        for (auto key :
             {"point_id", "point_name", "trigger_condition", "recovery_condition", "trigger_value"})
            if (row.contains(key) && row.at(key).is_string()) data[key] = row.at(key);
        DeviceRequest request(database_, actor, ctx, false);
        request.principal.require(permission);
        if (station_id(request) != station) missing();
        try {
            request.device(state->device_id.value());
        } catch (const ApiError& error) {
            if (error.status == 404) missing();
            throw;
        }
        current_value(data, request, realtime_config_);
        return request.finish({std::move(data), archive_meta(progress, request)});
    }

    const bool exporting = path == "/api/alarms/export";
    const auto filter = parse_filters(body, exporting);
    core::StationId station;
    AlarmArchiveProgress progress;
    std::vector<AlarmArchiveItem> pending;
    if (filter.history) {
        DeviceRequest request(database_, actor, ctx, false);
        request.principal.require(permission);
        if (!filter.root.empty() && !request.navigation_visible(filter.root)) missing();
        station = station_id(request);
        progress = from_repository(repo.progress(*request.transaction, station, ctx));
        pending = from_repository(repo.pending(*request.transaction, station, progress, {}, ctx));
        request.finish({});
    }

    std::map<std::string, ArchivedAlarmRow> history;
    if (filter.history) {
        auto series = open_series();
        auto rows = alarm_value(
            series->read_alarms(
                {station, progress.archived_seq, {}, filter.start_us, filter.end_us}, ctx),
            "alarm_archive_unavailable");
        for (auto& item : pending) {
            const auto row = object(item.state_json, station, item.alarm_id, item.device_id);
            const auto occurred = millis(row.value("occurred_at_us", Json()));
            if (occurred >= filter.start_us / 1000 && occurred <= filter.end_us / 1000)
                rows.push_back(
                    {item.alarm_id, item.device_id, item.stream_seq, std::move(item.state_json)});
        }
        history = latest(std::move(rows));
    }

    DeviceRequest request(database_, actor, ctx, false);
    request.principal.require(permission);
    if (station.empty()) station = station_id(request);
    if (station_id(request) != station ||
        (!filter.root.empty() && !request.navigation_visible(filter.root)))
        missing();
    DeviceScope scope(request);
    if (!filter.history)
        progress = from_repository(repo.progress(*request.transaction, station, ctx));
    Json counts = Json::object();
    Json summary = {{"total", 0},    {"active", 0},  {"cleared", 0}, {"unacknowledged", 0},
                    {"critical", 0}, {"warning", 0}, {"info", 0}};
    std::vector<Json> matched;
    const auto append = [&](const auto& path, const auto& id, const auto& device,
                            const auto& payload) {
        if (!filter.root.empty() && std::find(path.begin(), path.end(), filter.root) == path.end())
            return;
        auto item = record(object(payload, station, id, device));
        if (!matches(item, filter)) return;
        increment(summary, "total");
        increment(summary, item.at("severity"));
        increment(summary, item.at("cleared_at_ms").is_null() ? "active" : "cleared");
        if (item.at("acknowledgement").is_null()) increment(summary, "unacknowledged");
        matched.push_back(std::move(item));
    };
    if (!exporting || !filter.history) {
        const auto active =
            from_repository(repo.active_snapshot(*request.transaction, station, ctx));
        for (const auto& row : active) {
            const auto chain = scope.authorized_path(row.device_id.value());
            if (chain.empty()) continue;
            for (const auto& node : chain) increment(counts, node);
            if (!filter.history) append(chain, row.alarm_id, row.device_id, row.state_json);
        }
    }
    for (const auto& [id, row] : history) {
        const auto chain = scope.authorized_path(row.device_id.value());
        if (!chain.empty()) append(chain, row.alarm_id, row.device_id, row.payload);
    }
    std::sort(matched.begin(), matched.end(), [](const Json& a, const Json& b) {
        if (a.at("occurred_at_ms") != b.at("occurred_at_ms"))
            return a.at("occurred_at_ms") > b.at("occurred_at_ms");
        return a.at("alarm_id") < b.at("alarm_id");
    });
    Json items = Json::array();
    for (auto i = filter.offset; i < matched.size() && i - filter.offset < filter.limit; ++i)
        items.push_back(std::move(matched[i]));
    if (exporting)
        return request.finish(
            {{{"items", std::move(items)}, {"truncated", matched.size() > filter.limit}},
             archive_meta(progress, request)});
    return request.finish({{{"items", std::move(items)},
                            {"total", matched.size()},
                            {"summary", summary},
                            {"device_counts", counts},
                            {"queried_at_ms", core::now_utc_micros() / 1000}},
                           archive_meta(progress, request)});
}

}  // namespace ems::web
