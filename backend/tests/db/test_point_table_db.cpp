#include <db/db_manager.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <util/excel.hpp>

#ifdef EMS_TEST_SQLITE
#include <sqlite3.h>
#endif

namespace ems::db {
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

#ifdef EMS_TEST_SQLITE
class PointDatabase : public PointExcel {
   protected:
    DbManager database;
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
        ASSERT_TRUE(database.connect_sqlite(file)) << database.last_error();
    }

    void TearDown() override {
        database.disconnect();
        sqlite3_close(raw);
        PointExcel::TearDown();
    }
};

TEST_F(PointDatabase, CommitsAllFourTablesAndFeedbackIds) {
    auto result = database.import_point_table(sample("modbus"), 1, 1);
    ASSERT_TRUE(result.committed) << database.last_error();
    EXPECT_EQ(result.inserted, 18u);
    const auto yc = database.query_yc_points(1);
    const auto yx = database.query_yx_points(1);
    const auto yk = database.query_yk_points(1);
    const auto yt = database.query_yt_points(1);
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
    EXPECT_TRUE(database.is_connected());
    database.disconnect();
    EXPECT_FALSE(database.is_connected());
}

TEST_F(PointDatabase, DuplicateAppendAndInvalidScopeLeaveExistingTableIntact) {
    ASSERT_TRUE(database.import_point_table(sample("modbus"), 1, 1).committed);
    EXPECT_FALSE(database.import_point_table(sample("modbus"), 1, 1).committed);
    EXPECT_FALSE(database.import_point_table(sample("modbus"), 1, 2).committed);
    EXPECT_FALSE(database.import_point_table(sample("modbus"), 2, 2).committed);
    EXPECT_EQ(database.query_yc_points(1).size(), 6u);
    EXPECT_TRUE(database.query_yc_points(2).empty());
}

TEST_F(PointDatabase, MissingFeedbackRollsBackAndSuccessfulReplaceResolvesNewIds) {
    ASSERT_TRUE(database.import_point_table(sample("modbus"), 1, 1).committed);
    const auto old = database.query_yc_points(1);
    const auto file = edited("遥控", "I2", std::string("MISSING"));
    const auto failed = database.import_point_table(file, 1, 1, PointTableImportMode::Replace);
    EXPECT_FALSE(failed.committed);
    EXPECT_FALSE(failed.ok());
    EXPECT_EQ(database.query_yc_points(1)[0].id, old[0].id);
    EXPECT_EQ(database.query_yk_points(1).size(), 3u);
    ASSERT_TRUE(database.import_point_table(sample("modbus"), 1, 1, PointTableImportMode::Replace)
                    .committed);
    const auto yx = database.query_yx_points(1);
    EXPECT_EQ(database.query_yk_points(1)[0].related_yx_id, yx[0].id);
}

TEST_F(PointDatabase, DatabaseFailureRollsBackPreviouslyWrittenRows) {
    sql("CREATE TRIGGER reject_command BEFORE INSERT ON point_yk BEGIN SELECT RAISE(ABORT,'test "
        "write failure'); END");
    const auto result = database.import_point_table(sample("modbus"), 1, 1);
    EXPECT_FALSE(result.committed);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.inserted, 0u);
    EXPECT_TRUE(database.query_yc_points(1).empty());
    EXPECT_TRUE(database.query_yx_points(1).empty());
    EXPECT_TRUE(database.query_yk_points(1).empty());
    EXPECT_TRUE(database.query_yt_points(1).empty());
    EXPECT_TRUE(database.last_error().empty());
}

TEST_F(PointDatabase, ReplacePreservesOtherChannelAndRejectsExternalFeedbackReferences) {
    ASSERT_TRUE(database.import_point_table(sample("modbus"), 1, 1).committed);
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
    ASSERT_TRUE(database.import_point_table(file.u8string(), 2, 2).committed);
    const auto other = database.query_yc_points(2);
    ASSERT_TRUE(database.import_point_table(sample("modbus"), 1, 1, PointTableImportMode::Replace)
                    .committed);
    EXPECT_EQ(database.query_yc_points(2)[0].id, other[0].id);
    EXPECT_EQ(database.query_yc_points(2).size(), 6u);
    const auto feedback = database.query_yx_points(1)[0].id;
    sql("UPDATE point_yk SET related_yx_id=" + std::to_string(feedback) + " WHERE channel_id=2");
    const auto failed =
        database.import_point_table(sample("modbus"), 1, 1, PointTableImportMode::Replace);
    EXPECT_FALSE(failed.committed);
    EXPECT_EQ(database.query_yx_points(1)[0].id, feedback);
}

TEST_F(PointDatabase, CrudUsesRealTableNamesAndBoundCodeParameters) {
    device dev;
    dev.id = 3;
    dev.code = "manufacturer's-pcs";
    ASSERT_TRUE(database.insert_device(dev));
    EXPECT_EQ(database.query_device_by_code(dev.code).id, 3);
    EXPECT_EQ(database.query_device_by_code("' OR 1=1 --").id, 0);
    dev.name = "新设备名称";
    ASSERT_TRUE(database.update_device(dev));
    EXPECT_EQ(database.query_device_by_id(3).name, dev.name);
    channel ch;
    ch.id = 3;
    ch.device_id = 3;
    ch.code = "channel's-code";
    ASSERT_TRUE(database.insert_channel(ch));
    EXPECT_EQ(database.query_channel_by_code(ch.code).id, 3);
    ch.name = "新通道名称";
    ASSERT_TRUE(database.update_channel(ch));
    EXPECT_EQ(database.query_channel_by_id(3).name, ch.name);
    EXPECT_TRUE(database.delete_channel(3));
    EXPECT_TRUE(database.delete_device(3));
}

TEST_F(PointDatabase, StoresIec104AsduAndDlt645DataIdentifier) {
    sql("UPDATE channel SET protocol_type=3 WHERE id=1");
    ASSERT_TRUE(database.import_point_table(sample("iec104"), 1, 1).committed)
        << database.last_error();
    EXPECT_EQ(database.query_yc_points(1)[0].iec104_type, "M_ME_NB_1");
    EXPECT_EQ(database.query_yc_points(1)[0].ioa, 16385);
    sql("UPDATE channel SET protocol_type=0 WHERE id=1");
    ASSERT_TRUE(database.import_point_table(sample("dlt645"), 1, 1, PointTableImportMode::Replace)
                    .committed)
        << database.last_error();
    EXPECT_EQ(database.query_yc_points(1)[2].data_id, 0x02010100);
    EXPECT_TRUE(database.query_yk_points(1).empty());
}
#endif

}  // namespace
}  // namespace ems::db
