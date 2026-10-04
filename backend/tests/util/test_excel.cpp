#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <util/excel.hpp>
#include <utility>

namespace {

using ems::util::Excel;
using ems::util::ExcelTable;
using ems::util::ExcelValue;

static_assert(!std::is_copy_constructible_v<Excel>);
static_assert(!std::is_copy_assignable_v<Excel>);
static_assert(std::is_nothrow_move_constructible_v<Excel>);
static_assert(std::is_nothrow_move_assignable_v<Excel>);

class ExcelTest : public testing::Test {
   protected:
    void SetUp() override {
        static std::atomic<unsigned> sequence{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory_ = std::filesystem::temp_directory_path() /
                     ("ems_excel_" + std::to_string(stamp) + "_" + std::to_string(sequence++));
        std::filesystem::create_directory(directory_);
        path_ = directory_ / "workbook.xlsx";
    }

    void TearDown() override {
        excel_.close();
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    std::filesystem::path directory_;
    std::filesystem::path path_;
    Excel excel_;
};

TEST_F(ExcelTest, ClosedOperationsAndRepeatedClose) {
    EXPECT_FALSE(excel_.isOpen());
    EXPECT_THROW(excel_.save(), std::logic_error);
    EXPECT_THROW(excel_.sheetNames(), std::logic_error);
    EXPECT_THROW(excel_.readCell("Sheet1", 1, 1), std::logic_error);
    excel_.close();
    excel_.close();
}

TEST_F(ExcelTest, TypedValuesRoundTripThroughFile) {
    excel_.create(path_);
    const ExcelTable values{
        {std::string(u8"设备名称"), std::int64_t{42}, 12.5, true, std::monostate{}},
        {std::string(u8"储能柜"), std::int64_t{-7}, -0.25, false, std::string("00123")}};
    excel_.writeRange("Sheet1", 2, 3, values);
    excel_.save();
    excel_.close();
    excel_.open(path_);
    EXPECT_EQ(excel_.readRange("Sheet1", 2, 3, 2, 5), values);
    EXPECT_EQ(excel_.usedSize("Sheet1"), (std::make_pair(std::uint32_t{3}, std::uint16_t{7})));
}

TEST_F(ExcelTest, UnicodePathsAndSheetNames) {
    const auto unicodePath = directory_ / std::filesystem::u8path(u8"电量报表.xlsx");
    excel_.create(unicodePath);
    excel_.renameSheet("Sheet1", u8"电量统计");
    excel_.writeCell(u8"电量统计", "A1", std::string(u8"中文内容"));
    excel_.save();
    excel_.close();
    excel_.open(unicodePath);
    EXPECT_EQ(excel_.readCell(u8"电量统计", "A1"), ExcelValue{std::string(u8"中文内容")});
}

TEST_F(ExcelTest, CreateAndSaveAsRejectAccidentalOverwrite) {
    excel_.create(path_);
    excel_.writeCell("Sheet1", "A1", std::string("original"));
    excel_.save();
    excel_.close();
    EXPECT_THROW(excel_.create(path_), std::exception);
    EXPECT_FALSE(excel_.isOpen());
    excel_.open(path_);
    EXPECT_EQ(excel_.readCell("Sheet1", "A1"), ExcelValue{std::string("original")});
    const auto copyPath = directory_ / "copy.xlsx";
    excel_.saveAs(copyPath);
    excel_.writeCell("Sheet1", "A1", std::string("copy"));
    EXPECT_THROW(excel_.saveAs(path_), std::exception);
    excel_.save();
    excel_.close();
    excel_.open(copyPath);
    EXPECT_EQ(excel_.readCell("Sheet1", "A1"), ExcelValue{std::string("copy")});
    excel_.saveAs(path_, true);
    excel_.close();
    excel_.create(copyPath, true);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(excel_.readCell("Sheet1", "A1")));
}

TEST_F(ExcelTest, OpenFailureCanBeRetried) {
    EXPECT_THROW(excel_.open(path_), std::exception);
    EXPECT_FALSE(excel_.isOpen());
    EXPECT_THROW(excel_.create(directory_ / "old.xls"), std::invalid_argument);
    excel_.create(path_);
    EXPECT_THROW(excel_.create(directory_ / "other.xlsx"), std::logic_error);
    EXPECT_THROW(excel_.open(path_), std::logic_error);
    EXPECT_TRUE(excel_.isOpen());
}

TEST_F(ExcelTest, SheetsCanBeAddedRenamedAndRemoved) {
    excel_.create(path_);
    excel_.addSheet("Data");
    EXPECT_TRUE(excel_.hasSheet("Data"));
    EXPECT_THROW(excel_.addSheet("Data"), std::exception);
    EXPECT_THROW(excel_.addSheet("bad/name"), std::exception);
    excel_.renameSheet("Data", "Report");
    EXPECT_FALSE(excel_.hasSheet("Data"));
    EXPECT_EQ(excel_.sheetNames(), (std::vector<std::string>{"Sheet1", "Report"}));
    EXPECT_THROW(excel_.renameSheet("Report", "Sheet1"), std::exception);
    excel_.removeSheet("Sheet1");
    EXPECT_THROW(excel_.removeSheet("Report"), std::logic_error);
    EXPECT_THROW(excel_.readCell("Missing", "A1"), std::exception);
    excel_.save();
    excel_.close();
    excel_.open(path_);
    EXPECT_EQ(excel_.sheetNames(), (std::vector<std::string>{"Report"}));
}

TEST_F(ExcelTest, ReadMissingCellsDoesNotGrowWorksheet) {
    excel_.create(path_);
    const auto size = excel_.usedSize("Sheet1");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(excel_.readCell("Sheet1", "XFD1048576")));
    const ExcelTable blank(2, ems::util::ExcelRow(2, std::monostate{}));
    EXPECT_EQ(excel_.readRange("Sheet1", 100, 100, 2, 2), blank);
    EXPECT_TRUE(excel_.readFormula("Sheet1", "ZZ999").empty());
    EXPECT_EQ(excel_.usedSize("Sheet1"), size);
}

TEST_F(ExcelTest, FormulaAndValueReplacementPersist) {
    excel_.create(path_);
    excel_.writeCell("Sheet1", "A1", std::int64_t{8});
    excel_.writeFormula("Sheet1", "B1", "=A1*2");
    EXPECT_EQ(excel_.readFormula("Sheet1", "B1"), "A1*2");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(excel_.readCell("Sheet1", "B1")));
    excel_.save();
    excel_.close();
    excel_.open(path_);
    EXPECT_EQ(excel_.readFormula("Sheet1", "B1"), "A1*2");
    excel_.writeCell("Sheet1", "B1", std::int64_t{16});
    EXPECT_TRUE(excel_.readFormula("Sheet1", "B1").empty());
    excel_.writeCell("Sheet1", "A1", std::monostate{});
    EXPECT_TRUE(std::holds_alternative<std::monostate>(excel_.readCell("Sheet1", "A1")));
    EXPECT_THROW(excel_.writeFormula("Sheet1", "B1", "="), std::invalid_argument);
}

TEST_F(ExcelTest, BoundsAndMalformedAddressesAreRejected) {
    excel_.create(path_);
    EXPECT_THROW(excel_.readCell("Sheet1", 0, 1), std::out_of_range);
    EXPECT_THROW(excel_.writeCell("Sheet1", 1, 0, true), std::out_of_range);
    EXPECT_THROW(excel_.readCell("Sheet1", 1048577, 1), std::out_of_range);
    EXPECT_THROW(excel_.readCell("Sheet1", 1, 16385), std::out_of_range);
    for (const auto* address : {"", "A", "A0", "A01", "1A", "$A$1", "a1", "A1:B2", "A-1"}) {
        EXPECT_THROW(excel_.readCell("Sheet1", address), std::invalid_argument) << address;
    }
    EXPECT_THROW(excel_.readCell("Sheet1", "XFE1"), std::out_of_range);
    EXPECT_THROW(excel_.readCell("Sheet1", "A1048577"), std::out_of_range);
    EXPECT_THROW(excel_.readCell("Sheet1", "AAAAAAAAAAAAAAAAAAAA1"), std::out_of_range);
    EXPECT_THROW(excel_.readRange("Sheet1", 1048576, 1, 2, 1), std::out_of_range);
    EXPECT_THROW(excel_.readRange("Sheet1", 1, 16384, 1, 2), std::out_of_range);
}

TEST_F(ExcelTest, InvalidTablesAreValidatedBeforeWriting) {
    excel_.create(path_);
    excel_.writeCell("Sheet1", "A1", std::int64_t{9});
    const ExcelTable ragged{{std::int64_t{1}}, {std::int64_t{2}, std::int64_t{3}}};
    EXPECT_THROW(excel_.writeRange("Sheet1", 1, 1, ragged), std::invalid_argument);
    const ExcelTable invalid{{std::int64_t{1}}, {std::numeric_limits<double>::infinity()}};
    EXPECT_THROW(excel_.writeRange("Sheet1", 1, 1, invalid), std::invalid_argument);
    EXPECT_EQ(excel_.readCell("Sheet1", "A1"), ExcelValue{std::int64_t{9}});
    EXPECT_THROW(excel_.writeCell("Sheet1", "A1", std::numeric_limits<double>::quiet_NaN()),
                 std::invalid_argument);
    EXPECT_THROW(excel_.writeCell("Sheet1", "A1", std::string("a\0b", 3)), std::invalid_argument);
    EXPECT_TRUE(excel_.readRange("Sheet1", 1, 1, 0, 1).empty());
    EXPECT_TRUE(excel_.readRange("Sheet1", 1, 1, 1, 0).empty());
    excel_.writeRange("Sheet1", 1, 1, {});
}

TEST_F(ExcelTest, MoveTransfersOwnershipAndSourceCanReopen) {
    excel_.create(path_);
    excel_.writeCell("Sheet1", "A1", std::string("moved"));
    Excel moved(std::move(excel_));
    EXPECT_FALSE(excel_.isOpen());
    EXPECT_TRUE(moved.isOpen());
    excel_.create(directory_ / "replacement.xlsx");
    excel_ = std::move(moved);
    EXPECT_FALSE(moved.isOpen());
    EXPECT_EQ(excel_.readCell("Sheet1", "A1"), ExcelValue{std::string("moved")});
    auto& same = excel_;
    excel_ = std::move(same);
    EXPECT_TRUE(excel_.isOpen());
    excel_.save();
}

TEST_F(ExcelTest, CloseAndDestructionDoNotSaveChanges) {
    excel_.create(path_);
    excel_.writeCell("Sheet1", "A1", std::int64_t{1});
    excel_.save();
    excel_.writeCell("Sheet1", "A1", std::int64_t{2});
    excel_.close();
    {
        Excel temporary;
        temporary.open(path_);
        temporary.writeCell("Sheet1", "A1", std::int64_t{3});
    }
    excel_.open(path_);
    EXPECT_EQ(excel_.readCell("Sheet1", "A1"), ExcelValue{std::int64_t{1}});
}

}  // namespace
