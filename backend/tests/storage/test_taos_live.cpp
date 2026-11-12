#include <common/codec/digest.h>
#include <common/core/time.h>
#include <common/database/mysql_database.h>
#include <common/database/timeseries/factory.h>
#include <gtest/gtest.h>
#include <taos.h>

#include <cstdlib>

#ifdef EMS_TAOS_WEB_LIVE
#include "web/modules/history/history_service.h"
#endif

namespace ems::database {
namespace {
TEST(TaosLive, TypedRoundTripCompositeIdentityAndCursor) {
    const auto config = std::getenv("EMS_TAOS_LIVE_CONFIG");
    if (!config) GTEST_SKIP() << "Requires an explicitly selected live TDengine configuration";
    auto loaded = load_time_series_options(config);
    ASSERT_TRUE(core::is_ok(loaded));
    const auto options = core::value_of(loaded);
    auto opened = connect_time_series(options);
    ASSERT_TRUE(core::is_ok(opened)) << (core::is_ok(opened) ? "" : core::error_of(opened).message);
    auto store = std::move(std::get<0>(opened));
    const auto now = core::now_utc_micros();
    const auto scope = "integration-" + std::to_string(now);
    std::vector<SeriesMapping> mappings;
    TAOS* admin = taos_connect(options.taos.host.c_str(), options.taos.user.c_str(),
                               options.taos.password.c_str(), options.taos.database.c_str(),
                               options.taos.port);
    ASSERT_NE(admin, nullptr);

    struct Cleanup {
        TAOS* connection;
        std::vector<SeriesMapping>& mappings;

        ~Cleanup() {
            // 仅删除本次随机测试身份生成的四张子表，不操作业务点或数据库。
            for (const auto& mapping : mappings) {
                auto result = taos_query(
                    connection,
                    ("DROP TABLE IF EXISTS ems_history.`" + mapping.series_key + "`").c_str());
                if (result) taos_free_result(result);
            }
            taos_close(connection);
        }
    } cleanup{admin, mappings};

    const auto context = [] { return core::make_call_context(std::chrono::seconds(10)); };
    std::vector<Sample> expected;
    for (const auto type :
         {ValueType::Numeric, ValueType::Integer, ValueType::Boolean, ValueType::Text}) {
        Sample sample;
        sample.point_id = scope + "-" + std::to_string(static_cast<int>(type));
        sample.event_id = "event-1";
        sample.series_key = "Live-Integration:" + std::to_string(now) +
                            ":typed:" + std::to_string(static_cast<int>(type));
        sample.scope_kind = "station";
        sample.scope_id = scope;
        sample.config_revision = "1";
        sample.source_id = "live-integration";
        sample.source_epoch = "0";
        sample.sequence = "1";
        sample.source_time_us = now;
        sample.ingest_time_us = now;
        sample.measurement_time_us = now - 1000000;
        sample.value_type = type;
        if (type == ValueType::Numeric) sample.value = std::stod("12.5");
        if (type == ValueType::Integer) sample.value = std::int64_t{9007199254740993LL};
        if (type == ValueType::Boolean) sample.value = true;
        if (type == ValueType::Text) sample.value = std::string("中文状态：正常");
        sample.payload_sha256 = codec::sha256(sample.point_id);
        mappings.push_back({sample.point_id, type, sample.series_key});
        auto result = store->write(sample, context());
        ASSERT_TRUE(core::is_ok(result))
            << (core::is_ok(result) ? "" : core::error_of(result).message);
        expected.push_back(sample);
    }
    auto second = expected[0];
    second.event_id = "event-2";
    second.value = std::monostate{};
    second.quality = 8;
    second.quality_reason = "sample_stale";
    ASSERT_TRUE(core::is_ok(store->write(second, context())));
    HistoryQuery query;
    query.scope_kind = "station";
    query.scope_id = scope;
    query.start_us = now - 1;
    query.end_us = now + 1;
    query.max_rows = 100;
    for (const auto& mapping : mappings) query.point_ids.push_back(mapping.point_id);
    auto scanned = store->scan(query, mappings, 100, context());
    ASSERT_TRUE(core::is_ok(scanned))
        << (core::is_ok(scanned) ? "" : core::error_of(scanned).message);
    const auto rows = core::value_of(scanned);
    ASSERT_EQ(rows.size(), 5u);
    EXPECT_EQ(rows[0].value, expected[0].value);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(rows[1].value));
    EXPECT_EQ(rows[1].quality, 8);
    EXPECT_EQ(rows[2].value, expected[1].value);
    EXPECT_EQ(rows[3].value, expected[2].value);
    EXPECT_EQ(rows[4].value, expected[3].value);
    EXPECT_EQ(rows[0].measurement_time_us, now - 1000000);
    QueryCursor cursor;
    cursor.source_time_us = rows[0].source_time_us;
    cursor.point_id = rows[0].point_id;
    cursor.event_id = rows[0].event_id;
    query.cursor = cursor;
    auto next = store->scan(query, mappings, 100, context());
    ASSERT_TRUE(core::is_ok(next));
    ASSERT_EQ(core::value_of(next).size(), 4u);
    EXPECT_EQ(core::value_of(next)[0].event_id, "event-2");
    std::vector<Sample> batch;
    std::vector<SeriesMapping> batch_mappings;
    for (int i = 0; i < 32; ++i) {
        auto sample = expected[0];
        sample.point_id = scope + "-batch-" + std::to_string(i);
        sample.series_key =
            "Live-Integration:" + std::to_string(now) + ":batch:" + std::to_string(i);
        sample.value = std::stod(std::to_string(i));
        batch_mappings.push_back({sample.point_id, sample.value_type, sample.series_key});
        mappings.push_back(batch_mappings.back());
        batch.push_back(std::move(sample));
    }
    const auto statuses = store->write_batch(batch, context());
    ASSERT_EQ(statuses.size(), batch.size());
    for (const auto& status : statuses)
        ASSERT_TRUE(core::is_ok(status))
            << (core::is_ok(status) ? "" : core::error_of(status).message);
    query.cursor.reset();
    query.point_ids.clear();
    for (const auto& mapping : batch_mappings) query.point_ids.push_back(mapping.point_id);
    auto bulk = store->scan(query, batch_mappings, 100, context());
    ASSERT_TRUE(core::is_ok(bulk));
    EXPECT_EQ(core::value_of(bulk).size(), 32u);

    auto longest = expected[0];
    longest.point_id = scope + "-longest";
    longest.series_key = "Live-Integration:" + std::string(64, 'u') + ":" + std::string(64, 'd') +
                         ":" + std::string(45, 'p');
    ASSERT_EQ(longest.series_key.size(), 192u);
    mappings.push_back({longest.point_id, longest.value_type, longest.series_key});
    ASSERT_TRUE(core::is_ok(store->write(longest, context())));
    auto single = store->scan(query, {mappings.back()}, 100, context());
    ASSERT_TRUE(core::is_ok(single));
    ASSERT_EQ(core::value_of(single).size(), 1u);
    EXPECT_EQ(core::value_of(single)[0].series_key, longest.series_key);
    longest.series_key += 'p';
    EXPECT_FALSE(core::is_ok(store->write(longest, context())));
}

#ifdef EMS_TAOS_WEB_LIVE
TEST(TaosLive, HistoryServiceReadsArchivedEngineeringPoints) {
    const auto series_config = std::getenv("EMS_TAOS_LIVE_CONFIG");
    const auto mysql_config = std::getenv("EMS_MYSQL_LIVE_CONFIG");
    if (!series_config || !mysql_config)
        GTEST_SKIP() << "Requires explicitly selected live configuration";
    auto options = load_mysql_options(mysql_config);
    ASSERT_TRUE(core::is_ok(options));
    const auto context = [] { return core::make_call_context(std::chrono::seconds(10)); };
    auto connected = connect_mysql_database(core::value_of(options), context());
    ASSERT_TRUE(core::is_ok(connected));
    auto db = core::value_of(connected);
    ems::web::HistoryService history(db, {}, series_config);
    ems::web::Principal actor{"live-integration", {"history.read"}, {"*"}, {"*"}};
    for (const auto& [table, kind] : {std::pair{DeviceTable::Yc, "yc"}, {DeviceTable::Yx, "yx"}}) {
        auto begun = db.database->begin(context());
        ASSERT_TRUE(core::is_ok(begun));
        auto transaction = std::move(std::get<0>(begun));
        DeviceQuery points;
        points.table = table;
        points.limit = 1;
        auto rows = db.device_management->query(*transaction, points, context());
        ASSERT_TRUE(core::is_ok(rows));
        ASSERT_EQ(core::value_of(rows).rows.size(), 1u);
        const auto identity = std::visit(
            [](const auto& row) -> std::pair<std::string, std::string> {
                using T = std::decay_t<decltype(row)>;
                if constexpr (std::is_same_v<T, CfgPointYcRow> || std::is_same_v<T, CfgPointYxRow>)
                    return {row.owner_device_id, row.point_id};
                else
                    return {};
            },
            core::value_of(rows).rows[0]);
        ASSERT_TRUE(core::is_ok(transaction->commit(context())));
        transaction.reset();
        const auto now = core::now_utc_micros() / 1000;
        ems::web::Json body{{"device_id", identity.first},
                            {"point_type", kind},
                            {"point_ids", {identity.second}},
                            {"start_ms", now - 300000},
                            {"end_ms", now + 1000},
                            {"max_rows", 1000},
                            {"interval_ms", 0}};
        const auto raw = history.execute(body, actor, context());
        ASSERT_FALSE(raw.data.at("items").empty()) << kind;
        for (const auto& item : raw.data.at("items")) {
            EXPECT_EQ(item.at("point_id"), identity.second);
            EXPECT_EQ(item.at("timestamp").get<std::int64_t>() % 5000, 0);
            if (item.at("value").is_null()) EXPECT_NE(item.at("quality_code"), 0);
        }
        body["interval_ms"] = 60000;
        const auto sampled = history.execute(body, actor, context());
        ASSERT_FALSE(sampled.data.at("items").empty());
        EXPECT_LE(sampled.data.at("items").size(), raw.data.at("items").size());
        auto denied = actor;
        denied.device_roots = {"unrelated-device"};
        EXPECT_THROW(history.execute(body, denied, context()), ems::web::ApiError);
    }
}
#endif
}  // namespace
}  // namespace ems::database
