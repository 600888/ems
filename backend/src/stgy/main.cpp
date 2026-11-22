#include <common/codec/digest.h>
#include <common/core/time.h>
#include <common/database/mysql_database.h>
#include <rtdb/client/point_client.h>
#include <rtdb/model/configuration_catalog.h>

#include <chrono>
#include <csignal>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <thread>

#include "common/log/default_logger.hpp"
#include "common/log/process_logger.hpp"

namespace {
using namespace ems;
using namespace ems::database;
using Json = nlohmann::json;

volatile std::sig_atomic_t stopping = 0;

void stop(int) { stopping = 1; }

template <class T>
T take(core::Result<T> result) {
    if (!core::is_ok(result)) throw core::error_of(result);
    return std::move(std::get<0>(result));
}

struct FaultPoint {
    std::string point_id, point_name, device_id, device_name, device_path;
    int stale_after_ms{0};
};

struct FaultSample {
    const FaultPoint* point;
    bool active;
    core::UtcMicros timestamp;
};

class FaultAlarmRunner {
   public:
    FaultAlarmRunner(const MySqlDatabaseContext& database, std::string realtime_config)
        : database_(database), realtime_config_(std::move(realtime_config)) {}

    std::size_t poll(const core::CallContext& ctx) {
        refresh(ctx);
        if (points_.empty()) return 0;
        if (!client_) client_ = take(rtdb::PointClient::open(realtime_config_, catalog_, ctx));
        std::vector<FaultSample> samples;
        for (const auto& point : points_) {
            const auto raw = client_->readPointJson("yx", point.point_id, ctx);
            if (!core::is_ok(raw)) {
                const auto& error = core::error_of(raw);
                if (error.code == core::ErrorCode::NotFound) continue;
                client_.reset();
                throw error;
            }
            const auto reading = Json::parse(core::value_of(raw));
            const auto timestamp = reading.at("timestamp").get<std::int64_t>();
            const auto now = core::now_utc_micros() / 1000;
            if (reading.at("quality") != static_cast<int>(model::Quality::Good) ||
                !reading.at("value").is_boolean() || timestamp <= 0 || timestamp > now + 300000 ||
                (point.stale_after_ms > 0 && now - timestamp > point.stale_after_ms))
                continue;
            samples.push_back({&point, reading.at("value").get<bool>(), timestamp * 1000});
        }
        if (samples.empty()) return 0;

        auto tx = take(database_.database->begin(ctx));
        const auto current = read_station(*tx, ctx);
        if (current.device_id != station_.device_id ||
            current.config_revision != station_.config_revision)
            return 0;
        const auto station_id = core::StationId(station_.device_id);
        const auto rows = take(database_.alarms->active_snapshot(*tx, station_id, ctx));
        std::map<std::string, const AlarmState*> active;
        for (const auto& row : rows)
            if (row.active_key) active.emplace(*row.active_key, &row);

        std::size_t count = 0;
        for (const auto& sample : samples) {
            const auto& point = *sample.point;
            const auto key = "fault_active:" + point.point_id;
            const auto entry = active.find(key);
            const auto* prior = entry == active.end() ? nullptr : entry->second;
            if (sample.active == (prior != nullptr)) continue;
            const auto device = core::DeviceId(point.device_id);
            if (prior && (prior->device_id != device || sample.timestamp <= prior->occurred_at_us))
                continue;

            AlarmChange event;
            event.station_id = station_id;
            event.device_id = device;
            event.event_time_us = sample.timestamp;
            if (sample.active) {
                auto instance = Json::array({station_.device_id, key, sample.timestamp}).dump();
                event.alarm_id = model::AlarmId(codec::sha256(instance));
                if (take(database_.alarms->find(*tx, station_id, event.alarm_id, ctx))) continue;
                event.event_id = core::EventId(codec::sha256("occurred:" + event.alarm_id.value()));
                event.detail = "故障总信号置位";
                event.change = AlarmOccurrence{key,
                                               "FAULT_ACTIVE",
                                               "设备故障",
                                               model::AlarmSeverity::Warning,
                                               point.device_name,
                                               point.device_path,
                                               event.detail,
                                               point.point_id,
                                               point.point_name,
                                               "故障总信号为真",
                                               "故障总信号为假",
                                               "true"};
            } else {
                event.alarm_id = prior->alarm_id;
                event.event_id = core::EventId(codec::sha256("cleared:" + event.alarm_id.value()));
                event.detail = "故障总信号复位";
                event.change = AlarmRecovery{};
            }
            if (take(database_.alarms->apply(*tx, event, ctx)) == AlarmChangeResult::Applied)
                ++count;
        }
        take(tx->commit(ctx));
        return count;
    }

   private:
    CfgDeviceRow read_station(ITransaction& tx, const core::CallContext& ctx) const {
        DeviceQuery query;
        query.table = DeviceTable::Device;
        query.filters = {{"node_kind", "station"}};
        query.limit = 2;
        query.for_share = true;
        const auto rows = take(database_.device_management->query(tx, query, ctx)).rows;
        if (rows.size() != 1 || !std::holds_alternative<CfgDeviceRow>(rows.front()))
            throw core::make_error(core::ErrorCode::NotReady, core::WriteEffect::NotApplicable,
                                   "unique station required");
        return std::get<CfgDeviceRow>(rows.front());
    }

    void refresh(const core::CallContext& ctx) {
        auto tx = take(database_.database->begin(ctx));
        auto station = read_station(*tx, ctx);
        if (catalog_ && station.device_id == station_.device_id &&
            station.config_revision == station_.config_revision) {
            take(tx->commit(ctx));
            return;
        }
        const auto config = take(database_.device_config->load_configuration(*tx, ctx));
        const auto catalog = take(rtdb::configuration_catalog(config));
        std::map<std::string, const CfgDeviceRow*> devices;
        for (const auto& row : config.devices) devices.emplace(row.device_id, &row);
        std::vector<FaultPoint> points;
        for (const auto& row : config.points_yx) {
            if (row.semantic_code != "fault_active" || row.value_type != "bool") continue;
            const auto* point = catalog->findPoint(core::PointId(row.point_id));
            if (!point || !point->enabled) continue;
            const auto& device = *devices.at(row.owner_device_id);
            points.push_back(
                {row.point_id, row.name, device.device_id, device.name, device.path,
                 row.stale_after_ms > 0 ? row.stale_after_ms : station.default_stale_after_ms});
        }
        take(tx->commit(ctx));
        station_ = std::move(station);
        catalog_ = catalog;
        points_ = std::move(points);
        client_.reset();
    }

    const MySqlDatabaseContext& database_;
    std::string realtime_config_;
    CfgDeviceRow station_;
    std::shared_ptr<const rtdb::RuntimeCatalog> catalog_;
    std::shared_ptr<rtdb::PointClient> client_;
    std::vector<FaultPoint> points_;
};
}  // namespace

int main(int argc, char** argv) {
    std::string db_config = "conf/db.json", realtime_config = "conf/realtime.json";
    bool once = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            std::cout << "ems-strategy [--db-config path] [--rtdb-config path] [--once]\n";
            return 0;
        }
        if (argument == "--db-config" && i + 1 < argc)
            db_config = argv[++i];
        else if (argument == "--rtdb-config" && i + 1 < argc)
            realtime_config = argv[++i];
        else if (argument == "--once")
            once = true;
        else {
            std::cerr << "ems-strategy: invalid arguments; use --help\n";
            return 2;
        }
    }
    const ProcessLogger logger("ems-strategy");
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
#ifdef _WIN32
    std::signal(SIGBREAK, stop);
#endif
    try {
        auto options = take(load_mysql_options(db_config));
        options.require_configuration_schema = true;
        options.require_alarm_schema = true;
        const auto database = take(
            connect_mysql_database(options, core::make_call_context(std::chrono::seconds(10))));
        FaultAlarmRunner runner(database, realtime_config);
        LOG_INFO("ems-strategy ready: fault_active alarm polling");
        while (!stopping) {
            try {
                const auto count = runner.poll(core::make_call_context(std::chrono::seconds(2)));
                if (count) LOG_INFO("ems-strategy alarm transitions: {}", count);
                if (once) return 0;
            } catch (const core::Error& error) {
                LOG_WARN("ems-strategy alarm polling deferred: code={}, effect={}, reason={}",
                         core::to_string(error.code), core::to_string(error.effect), error.message);
                if (once) return 1;
            }
            for (int i = 0; i < 10 && !stopping; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    } catch (const core::Error& error) {
        LOG_ERROR("ems-strategy unavailable: {}", error.message);
        return 1;
    } catch (const std::exception&) {
        LOG_ERROR("ems-strategy: invalid alarm input or runtime configuration");
        return 1;
    }
    return 0;
}
