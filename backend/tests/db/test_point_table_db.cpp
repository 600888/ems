#include <common/database/configuration/repository.h>
#include <common/database/mysql_database.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <util/excel.hpp>

#include "common/db/runtime/transaction_database.h"
#include "net/para_loader/para_loader.h"

#ifdef EMS_TEST_SQLITE
#include <sqlite3.h>

#include <ormpp/sqlite.hpp>

#include "common/db/configuration/orm_repository.h"
#endif

namespace ems::database {
namespace {

std::string sample(const std::string& protocol) {
    return std::string(EMS_POINT_DATA_DIR) + "/point_csv/point_sample_" + protocol + ".xlsx";
}

class PointExcel : public ::testing::Test {
   protected:
    std::filesystem::path dir;

    void SetUp() override {
        dir = std::filesystem::temp_directory_path() /
              std::filesystem::u8path(
                  std::string("ems点表测试_") +
                  ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::create_directories(dir);
    }

    void TearDown() override { std::filesystem::remove_all(dir); }

    std::string edited(const std::string& sheet, const std::string& cell,
                       const util::ExcelValue& value) {
        util::Excel excel;
        excel.open(std::filesystem::u8path(sample("modbus")));
        excel.writeCell(sheet, cell, value);
        const auto file = dir / std::filesystem::u8path("设备点表.xlsx");
        excel.saveAs(file, true);
        return file.u8string();
    }
};

TEST_F(PointExcel, ReadsActualTemplatesForThreeProtocols) {
    const auto modbus = parse_point_table_excel(sample("modbus"), 2, 1, 1);
    ASSERT_TRUE(modbus.ok()) << modbus.errors.front().reason;
    EXPECT_EQ(modbus.data.yc.size(), 6u);
    EXPECT_EQ(modbus.data.yx.size(), 5u);
    EXPECT_EQ(modbus.data.yk.size(), 3u);
    EXPECT_EQ(modbus.data.yt.size(), 4u);
    EXPECT_EQ(modbus.data.yc[0].decode_code, 3);
    EXPECT_DOUBLE_EQ(modbus.data.yc[0].mul_coe, 0.1);
    EXPECT_EQ(modbus.data.yc[4].slave_id, 2);
    EXPECT_EQ(modbus.data.yk_feedback.at("PCS_START"), "PCS_RUN");
    const auto iec104 = parse_point_table_excel(sample("iec104"), 3, 1, 1);
    ASSERT_TRUE(iec104.ok()) << iec104.errors.front().reason;
    EXPECT_EQ(iec104.data.yx.size(), 3u);
    EXPECT_EQ(iec104.data.yc[0].ioa, 16385);
    EXPECT_EQ(iec104.data.yc[0].iec104_type, "M_ME_NB_1");
    const auto meter = parse_point_table_excel(sample("dlt645"), 0, 1, 1);
    ASSERT_TRUE(meter.ok()) << meter.errors.front().reason;
    EXPECT_EQ(meter.data.yc.size(), 4u);
    EXPECT_EQ(meter.data.yc[2].data_id, 0x02010100);
}

TEST_F(PointExcel, ReportsExactSheetRowAndColumn) {
    const auto file = edited("遥测", "F2", std::string("UNKNOWN"));
    const auto result = parse_point_table_excel(file, 2, 1, 1);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.errors[0].sheet, "遥测");
    EXPECT_EQ(result.errors[0].line, 2u);
    EXPECT_EQ(result.errors[0].column, "解析码");
}

TEST_F(PointExcel, PreservesByteOrderAndRejectsWrongWidth) {
    auto file = edited("遥测", "F2", std::string("FLOAT_CDAB"));
    auto result = parse_point_table_excel(file, 2, 1, 1);
    ASSERT_TRUE(result.ok()) << result.errors.front().reason;
    EXPECT_EQ(result.data.yc[0].decode_code, 4);
    EXPECT_EQ(result.data.yc[0].endian, 1);
    file = edited("遥测", "F2", std::string("INT16_ABCD"));
    EXPECT_FALSE(parse_point_table_excel(file, 2, 1, 1).ok());
}

TEST_F(PointExcel, RejectsFormulaDuplicateCodeRangeAndProtocolMismatch) {
    auto file = edited("遥测", "A3", std::string("PCS_DC_V"));
    EXPECT_FALSE(parse_point_table_excel(file, 2, 1, 1).ok());
    file = edited("遥测", "D2", int64_t{65535});
    EXPECT_FALSE(parse_point_table_excel(file, 2, 1, 1).ok());
    file = edited("遥测", "G2", std::string("nan"));
    EXPECT_FALSE(parse_point_table_excel(file, 2, 1, 1).ok());
    util::Excel excel;
    excel.open(std::filesystem::u8path(file));
    excel.writeFormula("遥测", "G2", "1+1");
    excel.save();
    excel.close();
    EXPECT_FALSE(parse_point_table_excel(file, 2, 1, 1).ok());
    EXPECT_FALSE(parse_point_table_excel(sample("opcua"), 2, 1, 1).ok());
    EXPECT_FALSE(parse_point_table_excel(sample("dnp3"), 2, 1, 1).ok());
    EXPECT_FALSE(parse_point_table_excel(sample("modbus"), 4, 1, 1).ok());
    EXPECT_FALSE(
        parse_point_table_excel(std::string(EMS_POINT_DATA_DIR) + "/point_template.xlsx", 2, 1, 1)
            .ok());
}

class MysqlEnvironment {
   public:
    MysqlEnvironment() {
        for (const auto* key : keys_) {
            const char* value = std::getenv(key);
            previous_.push_back(value ? std::optional<std::string>(value) : std::nullopt);
            set(key, nullptr);
        }
    }

    ~MysqlEnvironment() {
        for (std::size_t i = 0; i < keys_.size(); ++i)
            set(keys_[i], previous_[i] ? previous_[i]->c_str() : nullptr);
    }

    static void set(const char* key, const char* value) {
#ifdef _WIN32
        _putenv_s(key, value ? value : "");
#else
        if (value)
            setenv(key, value, 1);
        else
            unsetenv(key);
#endif
    }

   private:
    const std::vector<const char*> keys_{"EMS_DATABASE_CONFIG",
                                         "EMS_MYSQL_HOST",
                                         "EMS_MYSQL_PORT",
                                         "EMS_MYSQL_USER",
                                         "EMS_MYSQL_PASSWORD",
                                         "EMS_MYSQL_DATABASE",
                                         "EMS_MYSQL_MAX_CONNECTIONS",
                                         "EMS_MYSQL_ACQUIRE_TIMEOUT_MS",
                                         "EMS_MYSQL_IO_TIMEOUT_SECONDS"};
    std::vector<std::optional<std::string>> previous_;
};

TEST_F(PointExcel, MysqlConfigurationIsStrictAndEnvironmentOverridesFile) {
    MysqlEnvironment environment;
    const auto file = dir / "database.json";
    {
        std::ofstream output(file);
        output
            << R"({"user":"file_user","database":"configuration","port":3307,"max_connections":2})";
    }
    MysqlEnvironment::set("EMS_MYSQL_USER", "environment_user");
    auto options = load_mysql_options(file.u8string());
    ASSERT_TRUE(core::is_ok(options));
    EXPECT_EQ(core::value_of(options).user, "environment_user");
    EXPECT_EQ(core::value_of(options).port, 3307);
    EXPECT_EQ(core::value_of(options).max_connections, 2u);
    MysqlEnvironment::set("EMS_MYSQL_PORT", "3306garbage");
    EXPECT_FALSE(core::is_ok(load_mysql_options(file.u8string())));
    MysqlEnvironment::set("EMS_MYSQL_PORT", "0");
    EXPECT_FALSE(core::is_ok(load_mysql_options(file.u8string())));
}

TEST_F(PointExcel, InvalidConfigurationDoesNotSilentlyFallBack) {
    MysqlEnvironment environment;
    EXPECT_FALSE(core::is_ok(load_mysql_options((dir / "absent.json").u8string())));
    const auto file = dir / "database.json";
    for (const auto* contents :
         {"{broken", "[]", R"({"user":"test","database":"test","port":3306.5})",
          R"({"user":"test","database":"test","max_connections":-1})"}) {
        {
            std::ofstream output(file);
            output << contents;
        }
        auto options = load_mysql_options(file.u8string());
        ASSERT_FALSE(core::is_ok(options));
        EXPECT_EQ(core::error_of(options).code, core::ErrorCode::InvalidArgument);
    }
}

#ifdef EMS_TEST_SQLITE
class SqliteTestConnection final : public detail::Connection {
   public:
    ormpp::sqlite orm;
    inline static bool unknown_commit = false;

    core::Status prepare(const core::CallContext& ctx) {
        if (core::has_expired(ctx))
            return core::make_error(core::ErrorCode::DeadlineExceeded,
                                    core::WriteEffect::NotApplied, "test deadline");
        return core::ok_status();
    }

    core::Status begin(const core::CallContext&) override { return status(orm.begin()); }

    core::Status commit(const core::CallContext&) override {
        auto result = status(orm.commit());
        if (unknown_commit && core::is_ok(result))
            return core::make_error(core::ErrorCode::Unavailable, core::WriteEffect::Unknown,
                                    "lost commit reply");
        return result;
    }

    core::Status rollback(const core::CallContext&) override { return status(orm.rollback()); }

    static core::Status status(bool ok) {
        return ok ? core::ok_status()
                  : core::Status{core::make_error(core::ErrorCode::Unavailable,
                                                  core::WriteEffect::Unknown,
                                                  "test database failure")};
    }
};

class PointDatabase : public PointExcel {
   protected:
    std::unique_ptr<detail::TransactionDatabase> database;
    std::unique_ptr<IConfigurationRepository> repository;
    std::string database_error;

    static auto context() { return core::make_call_context(std::chrono::seconds(5)); }

    PointTableImportResult import_points(const std::string& path, std::int64_t channel,
                                         std::int64_t device,
                                         PointTableImportMode mode = PointTableImportMode::Append) {
        auto result = import_configuration_points(*database, *repository, path, channel, device,
                                                  mode, context());
        if (core::is_ok(result)) {
            database_error.clear();
            return core::value_of(result);
        }
        database_error = core::error_of(result).message;
        PointTableImportResult failed;
        failed.errors.push_back({{}, 0, "database", database_error});
        return failed;
    }

    PointTableData points(std::int64_t channel) {
        auto begun = database->begin(context());
        EXPECT_TRUE(core::is_ok(begun));
        if (!core::is_ok(begun)) return {};
        auto tx = std::move(std::get<0>(begun));
        auto read = repository->read_configuration(*tx, context());
        EXPECT_TRUE(core::is_ok(read));
        if (!core::is_ok(read)) return {};
        auto records = core::value_of(read).points;
        auto filter = [&](auto& values) {
            values.erase(std::remove_if(values.begin(), values.end(),
                                        [&](const auto& row) { return row.channel_id != channel; }),
                         values.end());
        };
        filter(records.yc);
        filter(records.yx);
        filter(records.yk);
        filter(records.yt);
        return records;
    }

    sqlite3* raw{nullptr};

    void sql(const std::string& command) {
        char* message = nullptr;
        const auto rc = sqlite3_exec(raw, command.c_str(), nullptr, nullptr, &message);
        const auto reason = message ? message : "";
        EXPECT_EQ(rc, SQLITE_OK) << reason;
        sqlite3_free(message);
    }

    void SetUp() override {
        PointExcel::SetUp();
        const auto file = (dir / "config.db").u8string();
        ASSERT_EQ(sqlite3_open(file.c_str(), &raw), SQLITE_OK);
        const std::string common =
            "id INTEGER PRIMARY KEY AUTOINCREMENT,code TEXT UNIQUE,name TEXT,"
            "channel_id INTEGER,device_id INTEGER,protocol_type INTEGER,func_code INTEGER,"
            "slave_id INTEGER,reg_addr INTEGER,bit_position INTEGER,data_id INTEGER,casdu INTEGER,"
            "ioa INTEGER,object_ref TEXT,decode_code INTEGER,endian INTEGER,iec104_type TEXT,";
        const std::string meta = "enable INTEGER,description TEXT,created_at TEXT,updated_at TEXT";
        const std::string numeric =
            "mul_coe REAL,add_coe REAL,min_limit REAL,max_limit REAL,unit TEXT,";
        sql("CREATE TABLE point_yc (" + common + numeric + meta + ")");
        sql("CREATE TABLE point_yx (" + common + "bit INTEGER,reverse_flag INTEGER," + meta + ")");
        sql("CREATE TABLE point_yk (" + common +
            "bit INTEGER,command_type INTEGER,related_yx_id INTEGER," + meta + ")");
        sql("CREATE TABLE point_yt (" + common + "related_yc_id INTEGER," + numeric + meta + ")");
        sql("CREATE TABLE device (id INTEGER PRIMARY KEY,code TEXT,name TEXT,device_type "
            "INTEGER,group_id INTEGER,description TEXT,enable INTEGER,created_at TEXT,updated_at "
            "TEXT)");
        sql("CREATE TABLE channel (id INTEGER PRIMARY KEY,code TEXT,name TEXT,device_id "
            "INTEGER,protocol_type INTEGER,conn_type INTEGER,ip TEXT,port INTEGER,com_port "
            "TEXT,baud_rate INTEGER,data_bits INTEGER,stop_bits INTEGER,parity TEXT,rtu_addr "
            "TEXT,timeout INTEGER,enable INTEGER,created_at TEXT,updated_at TEXT)");
        sql("INSERT INTO device(id,code) VALUES (1,'pcs01'),(2,'pcs02')");
        sql("INSERT INTO channel(id,device_id,protocol_type) VALUES (1,1,2),(2,2,2)");
        database = std::make_unique<detail::TransactionDatabase>(
            detail::TransactionOptions{},
            [file](const core::CallContext&) -> core::Result<detail::ConnectionLease> {
                auto connection = std::make_shared<SqliteTestConnection>();
                if (!connection->orm.connect("", "", "", file, {}, {}))
                    return core::make_error(core::ErrorCode::Unavailable,
                                            core::WriteEffect::NotApplied, "test connect failed");
                connection->orm.set_enable_transaction(false);
                return detail::ConnectionLease{connection,
                                               [](std::shared_ptr<detail::Connection>, bool) {}};
            });
        repository = std::make_unique<detail::OrmConfigurationRepository<SqliteTestConnection>>(
            [this](ITransaction& tx,
                   const core::CallContext& ctx) -> core::Result<SqliteTestConnection*> {
                auto connection = database->connection(tx, ctx);
                if (!core::is_ok(connection)) return core::error_of(connection);
                return static_cast<SqliteTestConnection*>(core::value_of(connection));
            },
            [this](ITransaction& tx) { database->invalidate(tx); },
            [](SqliteTestConnection&) {
                return core::make_error(core::ErrorCode::Conflict, core::WriteEffect::NotApplied,
                                        "test SQL operation failed");
            },
            false);
    }

    void TearDown() override {
        repository.reset();
        database.reset();
        sqlite3_close(raw);
        PointExcel::TearDown();
    }
};

TEST_F(PointDatabase, CommitsAllFourTablesAndFeedbackIds) {
    auto result = import_points(sample("modbus"), 1, 1);
    ASSERT_TRUE(result.committed) << database_error;
    EXPECT_EQ(result.inserted, 18u);
    const auto yc = points(1).yc;
    const auto yx = points(1).yx;
    const auto yk = points(1).yk;
    const auto yt = points(1).yt;
    ASSERT_EQ(yc.size(), 6u);
    ASSERT_EQ(yx.size(), 5u);
    ASSERT_EQ(yk.size(), 3u);
    ASSERT_EQ(yt.size(), 4u);
    EXPECT_EQ(yk[0].related_yx_id, yx[0].id);
    EXPECT_EQ(yt[0].related_yc_id, yc[2].id);
    EXPECT_EQ(yc[0].decode_code, 3);
    EXPECT_DOUBLE_EQ(yc[0].mul_coe, 0.1);
    EXPECT_EQ(yc[0].channel_id, 1);
    EXPECT_EQ(yc[0].device_id, 1);
    EXPECT_GT(yc[0].id, 0);
}

TEST_F(PointDatabase, DuplicateAppendAndInvalidScopeLeaveExistingTableIntact) {
    ASSERT_TRUE(import_points(sample("modbus"), 1, 1).committed);
    EXPECT_FALSE(import_points(sample("modbus"), 1, 1).committed);
    EXPECT_FALSE(import_points(sample("modbus"), 1, 2).committed);
    EXPECT_FALSE(import_points(sample("modbus"), 2, 2).committed);
    EXPECT_EQ(points(1).yc.size(), 6u);
    EXPECT_TRUE(points(2).yc.empty());
}

TEST_F(PointDatabase, MissingFeedbackRollsBackAndSuccessfulReplaceResolvesNewIds) {
    ASSERT_TRUE(import_points(sample("modbus"), 1, 1).committed);
    const auto old = points(1).yc;
    const auto file = edited("遥控", "I2", std::string("MISSING"));
    const auto failed = import_points(file, 1, 1, PointTableImportMode::Replace);
    EXPECT_FALSE(failed.committed);
    EXPECT_FALSE(failed.ok());
    EXPECT_EQ(points(1).yc[0].id, old[0].id);
    EXPECT_EQ(points(1).yk.size(), 3u);
    ASSERT_TRUE(import_points(sample("modbus"), 1, 1, PointTableImportMode::Replace).committed);
    const auto yx = points(1).yx;
    EXPECT_EQ(points(1).yk[0].related_yx_id, yx[0].id);
}

TEST_F(PointDatabase, DatabaseFailureRollsBackPreviouslyWrittenRows) {
    sql("CREATE TRIGGER reject_command BEFORE INSERT ON point_yk BEGIN SELECT RAISE(ABORT,'test "
        "write failure'); END");
    const auto result = import_points(sample("modbus"), 1, 1);
    EXPECT_FALSE(result.committed);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.inserted, 0u);
    EXPECT_TRUE(points(1).yc.empty());
    EXPECT_TRUE(points(1).yx.empty());
    EXPECT_TRUE(points(1).yk.empty());
    EXPECT_TRUE(points(1).yt.empty());
}

TEST_F(PointDatabase, ReplacePreservesOtherChannelAndRejectsExternalFeedbackReferences) {
    ASSERT_TRUE(import_points(sample("modbus"), 1, 1).committed);
    util::Excel excel;
    excel.open(std::filesystem::u8path(sample("modbus")));
    for (const auto& sheet : excel.sheetNames()) {
        const auto size = excel.usedSize(sheet);
        for (std::uint32_t row = 2; row <= size.first; ++row) {
            const auto code = excel.readCell(sheet, row, 1);
            if (!std::holds_alternative<std::string>(code)) continue;
            excel.writeCell(sheet, row, 1, "other_" + std::get<std::string>(code));
            const auto feedback_column = sheet == "遥控" ? 9 : (sheet == "遥调" ? 11 : 0);
            if (feedback_column != 0) {
                const auto feedback = excel.readCell(sheet, row, feedback_column);
                if (std::holds_alternative<std::string>(feedback) &&
                    !std::get<std::string>(feedback).empty()) {
                    excel.writeCell(sheet, row, feedback_column,
                                    "other_" + std::get<std::string>(feedback));
                }
            }
        }
    }
    const auto file = dir / "other.xlsx";
    excel.saveAs(file, true);
    excel.close();
    ASSERT_TRUE(import_points(file.u8string(), 2, 2).committed);
    const auto other = points(2).yc;
    ASSERT_TRUE(import_points(sample("modbus"), 1, 1, PointTableImportMode::Replace).committed);
    EXPECT_EQ(points(2).yc[0].id, other[0].id);
    EXPECT_EQ(points(2).yc.size(), 6u);
    const auto feedback = points(1).yx[0].id;
    sql("UPDATE point_yk SET related_yx_id=" + std::to_string(feedback) + " WHERE channel_id=2");
    const auto failed = import_points(sample("modbus"), 1, 1, PointTableImportMode::Replace);
    EXPECT_FALSE(failed.committed);
    EXPECT_EQ(points(1).yx[0].id, feedback);
}

TEST_F(PointDatabase, CrudUsesRealTableNamesAndBoundCodeParameters) {
    auto begun = database->begin(context());
    ASSERT_TRUE(core::is_ok(begun));
    auto tx = std::move(std::get<0>(begun));
    DeviceRecord dev;
    dev.id = 3;
    dev.code = "manufacturer's-pcs";
    ASSERT_TRUE(
        core::is_ok(repository->write_device(*tx, dev, RecordWriteMode::Insert, context())));
    auto found = repository->find_device_by_code(*tx, dev.code, context());
    ASSERT_TRUE(core::is_ok(found));
    EXPECT_EQ(core::value_of(found).id, 3);
    dev.name = "新设备名称";
    ASSERT_TRUE(
        core::is_ok(repository->write_device(*tx, dev, RecordWriteMode::Update, context())));
    ChannelRecord ch;
    ch.id = 3;
    ch.device_id = 3;
    ch.code = "channel's-code";
    ASSERT_TRUE(
        core::is_ok(repository->write_channel(*tx, ch, RecordWriteMode::Insert, context())));
    found = repository->find_device(*tx, 3, context());
    ASSERT_TRUE(core::is_ok(found));
    EXPECT_EQ(core::value_of(found).name, dev.name);
    ch.name = "新通道名称";
    ASSERT_TRUE(
        core::is_ok(repository->write_channel(*tx, ch, RecordWriteMode::Update, context())));
    auto saved_channel = repository->find_channel_by_code(*tx, ch.code, context());
    ASSERT_TRUE(core::is_ok(saved_channel));
    EXPECT_EQ(core::value_of(saved_channel).name, ch.name);
    ASSERT_TRUE(core::is_ok(repository->remove_channel(*tx, 3, context())));
    ASSERT_TRUE(core::is_ok(repository->remove_device(*tx, 3, context())));
    ASSERT_TRUE(core::is_ok(tx->commit(context())));
    begun = database->begin(context());
    tx = std::move(std::get<0>(begun));
    auto injection = repository->find_device_by_code(*tx, "' OR 1=1 --", context());
    ASSERT_FALSE(core::is_ok(injection));
    EXPECT_EQ(core::error_of(injection).code, core::ErrorCode::NotFound);
}

TEST_F(PointDatabase, RepositoryStagesWithoutCommittingAndDestructorRollsBack) {
    auto parsed = parse_point_table_excel(sample("modbus"), 2, 1, 1);
    ASSERT_TRUE(parsed.ok());
    auto begun = database->begin(context());
    ASSERT_TRUE(core::is_ok(begun));
    auto tx = std::move(std::get<0>(begun));
    auto staged = repository->stage_point_table(*tx, parsed.data, 1, 1,
                                                PointTableImportMode::Append, context());
    ASSERT_TRUE(core::is_ok(staged));
    EXPECT_GT(core::value_of(staged).yc[0].id, 0);
    tx.reset();
    EXPECT_TRUE(points(1).yc.empty());
    EXPECT_TRUE(points(1).yx.empty());
    EXPECT_TRUE(import_points(sample("modbus"), 1, 1).committed);
}

TEST_F(PointDatabase, InvalidDraftPreventsCommitAndExpiredReadIsRejected) {
    auto parsed = parse_point_table_excel(sample("modbus"), 2, 1, 1);
    ASSERT_TRUE(parsed.ok());
    parsed.data.yc[0].device_id = 2;
    auto begun = database->begin(context());
    auto tx = std::move(std::get<0>(begun));
    auto staged = repository->stage_point_table(*tx, parsed.data, 1, 1,
                                                PointTableImportMode::Append, context());
    ASSERT_FALSE(core::is_ok(staged));
    EXPECT_EQ(core::error_of(staged).code, core::ErrorCode::InvalidArgument);
    EXPECT_FALSE(core::is_ok(tx->commit(context())));
    EXPECT_TRUE(points(1).yc.empty());
    begun = database->begin(context());
    tx = std::move(std::get<0>(begun));
    auto expired = repository->read_configuration(*tx, core::immediate_context());
    ASSERT_FALSE(core::is_ok(expired));
    EXPECT_EQ(core::error_of(expired).code, core::ErrorCode::DeadlineExceeded);
}

TEST_F(PointDatabase, MissingSchemaReturnsErrorInsteadOfEmptyConfiguration) {
    sql("DROP TABLE point_yc");
    auto begun = database->begin(context());
    auto tx = std::move(std::get<0>(begun));
    EXPECT_FALSE(core::is_ok(repository->read_configuration(*tx, context())));
    EXPECT_FALSE(core::is_ok(tx->commit(context())));
}

TEST_F(PointDatabase, ParameterLoaderUsesPublicContextAndRetainsLastSuccessfulConfiguration) {
    ASSERT_TRUE(import_points(sample("modbus"), 1, 1).committed);
    sql("UPDATE device SET enable=1");
    sql("UPDATE channel SET enable=1");
    MySqlDatabaseContext context;
    context.database = std::shared_ptr<IDatabase>(database.get(), [](auto*) {});
    context.configuration =
        std::shared_ptr<IConfigurationRepository>(repository.get(), [](auto*) {});
    net::device::ParaLoader loader;
    ASSERT_TRUE(loader.initialize(context, this->context()));
    EXPECT_EQ(loader.load_devices().size(), 2u);
    EXPECT_EQ(loader.load_channels().size(), 2u);
    EXPECT_EQ(loader.load_yc_points(1).size(), 6u);
    sql("DROP TABLE point_yc");
    EXPECT_FALSE(loader.reload(this->context()));
    EXPECT_FALSE(loader.last_error().empty());
    EXPECT_EQ(loader.load_yc_points(1).size(), 6u);
}

TEST_F(PointDatabase, ParameterLoaderDoesNotTreatReadFailureAsSuccessfulEmptyConfiguration) {
    sql("DROP TABLE point_yc");
    MySqlDatabaseContext context;
    context.database = std::shared_ptr<IDatabase>(database.get(), [](auto*) {});
    context.configuration =
        std::shared_ptr<IConfigurationRepository>(repository.get(), [](auto*) {});
    net::device::ParaLoader loader;
    EXPECT_FALSE(loader.initialize(context, this->context()));
    EXPECT_FALSE(loader.is_initialized());
    EXPECT_FALSE(loader.last_error().empty());
}

TEST_F(PointDatabase, ReferencedDeviceCannotBeDeleted) {
    auto begun = database->begin(context());
    auto tx = std::move(std::get<0>(begun));
    auto removed = repository->remove_device(*tx, 1, context());
    ASSERT_FALSE(core::is_ok(removed));
    EXPECT_EQ(core::error_of(removed).code, core::ErrorCode::Conflict);
    EXPECT_FALSE(core::is_ok(tx->commit(context())));
}

TEST_F(PointDatabase, UnknownCommitReturnsErrorAndDoesNotReplayImport) {
    SqliteTestConnection::unknown_commit = true;
    auto result = import_configuration_points(*database, *repository, sample("modbus"), 1, 1,
                                              PointTableImportMode::Append, context());
    SqliteTestConnection::unknown_commit = false;
    ASSERT_FALSE(core::is_ok(result));
    EXPECT_EQ(core::error_of(result).effect, core::WriteEffect::Unknown);
    EXPECT_EQ(points(1).yc.size(), 6u);
}

TEST_F(PointDatabase, WhereConditionsRemainBoundWhenOrderedAndLimited) {
    const auto filtered =
        ormpp::generate_query_sql<ChannelRecord>("device_id=? ORDER BY id LIMIT 2");
    EXPECT_NE(filtered.find("where"), std::string::npos);
    EXPECT_NE(filtered.find("device_id=? ORDER BY id LIMIT 2"), std::string::npos);
    const auto ordered = ormpp::generate_query_sql<ChannelRecord>("ORDER BY id LIMIT 2");
    EXPECT_EQ(ordered.find("where"), std::string::npos);
}

TEST_F(PointDatabase, StoresIec104AsduAndDlt645DataIdentifier) {
    sql("UPDATE channel SET protocol_type=3 WHERE id=1");
    ASSERT_TRUE(import_points(sample("iec104"), 1, 1).committed) << database_error;
    EXPECT_EQ(points(1).yc[0].iec104_type, "M_ME_NB_1");
    EXPECT_EQ(points(1).yc[0].ioa, 16385);
    sql("UPDATE channel SET protocol_type=0 WHERE id=1");
    ASSERT_TRUE(import_points(sample("dlt645"), 1, 1, PointTableImportMode::Replace).committed)
        << database_error;
    EXPECT_EQ(points(1).yc[2].data_id, 0x02010100);
    EXPECT_TRUE(points(1).yk.empty());
}
#endif

}  // namespace
}  // namespace ems::database
