#include <common/database/timeseries/factory.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <limits>

namespace ems::database {
namespace {
core::CallContext context() { return core::make_call_context(std::chrono::seconds(10)); }

class InfluxDbTest : public ::testing::Test {
   protected:
    InfluxDbConnectionOptions options;
    std::unique_ptr<ITimeSeriesDatabase> database;

    void SetUp() override {
        const auto* url = std::getenv("EMS_INFLUXDB_TEST_URL");
        ASSERT_NE(url, nullptr);
        options.url = url;
        options.token = "test-secret";
        auto result = connect_influxdb_series(options);
        ASSERT_TRUE(core::is_ok(result));
        database = std::move(std::get<0>(result));
    }

    Sample sample(ValueType type = ValueType::Numeric) {
        Sample row;
        row.point_id = "point-" + std::to_string(static_cast<int>(type));
        row.series_key =
            "Integration:" +
            std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + ":" +
            row.point_id;
        row.event_id = "event-a";
        row.scope_kind = "station";
        row.scope_id = "scope'\\\n测试";
        row.source_time_us = 1700000000123456;
        row.ingest_time_us = row.source_time_us + 100;
        row.measurement_time_us = row.source_time_us - 123;
        row.config_revision = "18";
        row.source_id = "collector";
        row.source_epoch = "1";
        row.sequence = "20";
        row.payload_sha256 = std::string(64, 'a');
        row.quality = 3;
        row.time_origin = 1;
        row.value_type = type;
        if (type == ValueType::Numeric) row.value = 1e-30;
        if (type == ValueType::Integer) row.value = std::int64_t{9007199254740993LL};
        if (type == ValueType::Boolean) row.value = true;
        if (type == ValueType::Text) row.value = std::string("中文\n\"状态\"\\\t");
        return row;
    }

    HistoryQuery query(const Sample& row) {
        HistoryQuery result;
        result.scope_kind = row.scope_kind;
        result.scope_id = row.scope_id;
        result.start_us = row.source_time_us - 1;
        result.end_us = row.source_time_us + 100;
        result.point_ids = {row.point_id};
        return result;
    }

    SeriesMapping mapping(const Sample& row) {
        return {row.point_id, row.value_type, row.series_key};
    }
};

TEST_F(InfluxDbTest, TypedBatchRoundTripAndIdempotency) {
    std::vector<Sample> expected;
    std::vector<SeriesMapping> mappings;
    for (const auto type :
         {ValueType::Numeric, ValueType::Integer, ValueType::Boolean, ValueType::Text}) {
        auto row = sample(type);
        mappings.push_back(mapping(row));
        expected.push_back(row);
        row.event_id = "event-b";
        row.value = std::monostate{};
        row.quality_reason = "missing";
        expected.push_back(row);
    }
    for (int retry = 0; retry < 2; ++retry) {
        const auto statuses = database->write_batch(expected, context());
        ASSERT_EQ(statuses.size(), expected.size());
        for (const auto& status : statuses) ASSERT_TRUE(core::is_ok(status));
    }
    const auto result = database->scan(query(expected[0]), mappings, 100, context());
    ASSERT_TRUE(core::is_ok(result));
    const auto& rows = core::value_of(result);
    ASSERT_EQ(rows.size(), expected.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        EXPECT_EQ(rows[i].value, expected[i].value);
        EXPECT_EQ(rows[i].event_id, expected[i].event_id);
        EXPECT_EQ(rows[i].quality_reason, expected[i].quality_reason);
        EXPECT_EQ(rows[i].source_time_us, expected[i].source_time_us);
        EXPECT_EQ(rows[i].measurement_time_us, expected[i].measurement_time_us);
        EXPECT_EQ(rows[i].config_revision, "18");
        EXPECT_EQ(rows[i].payload_sha256, expected[i].payload_sha256);
    }
}

TEST_F(InfluxDbTest, CursorOrdersEqualTimestampEventsAcrossPoints) {
    auto first = sample();
    auto second = first;
    second.event_id = "event-b";
    auto third = first;
    third.point_id = "point-9";
    third.series_key += "9";
    auto fourth = first;
    fourth.source_time_us += 1;
    const auto statuses = database->write_batch({second, first, third, fourth}, context());
    for (const auto& status : statuses) ASSERT_TRUE(core::is_ok(status));
    auto request = query(first);
    std::vector<std::string> events;
    for (int i = 0; i < 4; ++i) {
        auto page = database->scan(request, {mapping(first), mapping(third)}, 1, context());
        ASSERT_TRUE(core::is_ok(page));
        const auto& rows = core::value_of(page);
        ASSERT_EQ(rows.size(), 1u);
        events.push_back(rows[0].point_id + ":" + rows[0].event_id);
        request.cursor = QueryCursor{rows[0].source_time_us, rows[0].point_id, rows[0].event_id};
    }
    EXPECT_EQ(events, (std::vector<std::string>{"point-0:event-a", "point-0:event-b",
                                                "point-9:event-a", "point-0:event-a"}));
    auto end = database->scan(request, {mapping(first), mapping(third)}, 1, context());
    ASSERT_TRUE(core::is_ok(end));
    EXPECT_TRUE(core::value_of(end).empty());
    request = query(first);
    request.end_us = fourth.source_time_us;
    auto exclusive = database->scan(request, {mapping(first)}, 100, context());
    ASSERT_TRUE(core::is_ok(exclusive));
    EXPECT_EQ(core::value_of(exclusive).size(), 2u);
}

TEST_F(InfluxDbTest, RejectsInvalidValuesAndExpiredCallsBeforeSending) {
    auto row = sample();
    row.value = std::numeric_limits<double>::infinity();
    auto invalid = database->write(row, context());
    ASSERT_FALSE(core::is_ok(invalid));
    EXPECT_EQ(core::error_of(invalid).effect, core::WriteEffect::NotApplied);
    row.value = std::int64_t{3};
    EXPECT_FALSE(core::is_ok(database->write(row, context())));
    row = sample();
    row.series_key = "bad'key";
    EXPECT_FALSE(core::is_ok(database->write(row, context())));
    row = sample();
    auto expired = database->write(row, core::immediate_context());
    ASSERT_FALSE(core::is_ok(expired));
    EXPECT_EQ(core::error_of(expired).code, core::ErrorCode::DeadlineExceeded);
    EXPECT_EQ(core::error_of(expired).effect, core::WriteEffect::NotApplied);
    EXPECT_FALSE(core::is_ok(database->scan(query(row), {mapping(row)}, 0, context())));
    EXPECT_FALSE(
        core::is_ok(database->scan(query(row), {mapping(row)}, 100, core::immediate_context())));
    EXPECT_TRUE(database->write_batch({}, context()).empty());
    EXPECT_EQ(database->write_batch(std::vector<Sample>(257, row), context()).size(), 257u);
}

TEST_F(InfluxDbTest, PartialWriteAndLostResponseAreUnknownAndNotRetained) {
    for (const auto* mode : {"partial", "disconnect"}) {
        options.database = mode;
        auto opened = connect_influxdb_series(options);
        ASSERT_TRUE(core::is_ok(opened));
        auto store = std::move(std::get<0>(opened));
        auto row = sample();
        row.event_id = "fail";
        auto result = store->write_batch({row, row}, context());
        ASSERT_EQ(result.size(), 2u);
        for (const auto& status : result) {
            ASSERT_FALSE(core::is_ok(status));
            EXPECT_EQ(core::error_of(status).effect, core::WriteEffect::Unknown);
            EXPECT_EQ(core::error_of(status).message.find("test-secret"), std::string::npos);
        }
        row.event_id = "success";
        ASSERT_TRUE(core::is_ok(store->write(row, context())));
    }
}

TEST_F(InfluxDbTest, RejectsCorruptAndQueryErrorResponses) {
    for (const auto* mode : {"malformed", "query_error", "wrong_identity", "wrong_time"}) {
        options.database = mode;
        auto opened = connect_influxdb_series(options);
        ASSERT_TRUE(core::is_ok(opened));
        auto row = sample();
        auto& store = *std::get<0>(opened);
        ASSERT_TRUE(core::is_ok(store.write(row, context())));
        auto result = store.scan(query(row), {mapping(row)}, 100, context());
        ASSERT_FALSE(core::is_ok(result));
        EXPECT_EQ(core::error_of(result).code, core::ErrorCode::CorruptData);
    }
}

TEST_F(InfluxDbTest, RequestDeadlineBoundsLibraryIo) {
    options.database = "slow";
    auto opened = connect_influxdb_series(options);
    ASSERT_TRUE(core::is_ok(opened));
    const auto start = std::chrono::steady_clock::now();
    auto result = std::get<0>(opened)->write(
        sample(), core::make_call_context(std::chrono::milliseconds(80)));
    ASSERT_FALSE(core::is_ok(result));
    EXPECT_EQ(core::error_of(result).effect, core::WriteEffect::Unknown);
    EXPECT_EQ(core::error_of(result).code, core::ErrorCode::DeadlineExceeded);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(1));
}

TEST_F(InfluxDbTest, ExactIntegerLimitsEmptyTextAndFalseRemainDistinctFromNull) {
    std::vector<Sample> rows;
    for (const auto value : {INT64_MIN, INT64_MAX}) {
        auto row = sample(ValueType::Integer);
        row.event_id = std::to_string(value);
        row.value = value;
        rows.push_back(row);
    }
    auto empty = sample(ValueType::Text);
    empty.value = std::string{};
    rows.push_back(empty);
    auto boolean = sample(ValueType::Boolean);
    boolean.value = false;
    rows.push_back(boolean);
    auto statuses = database->write_batch(rows, context());
    for (const auto& status : statuses) ASSERT_TRUE(core::is_ok(status));
    for (const auto& row : rows) {
        auto read = database->scan(query(row), {mapping(row)}, 100, context());
        ASSERT_TRUE(core::is_ok(read));
        bool found = false;
        for (const auto& actual : core::value_of(read)) {
            if (actual.event_id == row.event_id) {
                found = true;
                EXPECT_EQ(actual.value, row.value);
            }
        }
        EXPECT_TRUE(found);
    }
}

TEST_F(InfluxDbTest, RejectsMoreThan1001EventsAtSameTimestamp) {
    auto row = sample();
    std::vector<Sample> batch;
    for (int i = 0; i < 1002; ++i) {
        row.event_id = "event-" + std::to_string(i);
        batch.push_back(row);
        if (batch.size() == 256 || i == 1001) {
            for (const auto& status : database->write_batch(batch, context()))
                ASSERT_TRUE(core::is_ok(status));
            batch.clear();
        }
    }
    auto read = database->scan(query(row), {mapping(row)}, 1, context());
    ASSERT_FALSE(core::is_ok(read));
    EXPECT_EQ(core::error_of(read).code, core::ErrorCode::ResourceExhausted);
}
}  // namespace
}  // namespace ems::database
