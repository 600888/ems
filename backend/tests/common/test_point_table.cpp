#include <common/config/point_table.h>
#include <common/model/device_models.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <util/excel.hpp>

namespace ems::config {
namespace {

constexpr const char* kHeader =
    "template_key,standard_code,vendor_name,address_space,address_offset,access,wire_type,"
    "byte_order,scale,offset,unit\n";

std::string power_row(const char* key = "power") {
    return std::string(key) +
           ",active_power,厂家功率,holding_register,0,read,f32,CDAB,-0.001,0,kW\n";
}

TEST(PointTableImport, ReadsNewFormatExcelAndRejectsFormulaWithoutChangingSource) {
    const auto path = std::filesystem::temp_directory_path() / "ems_point_table_v1.xlsx";
    util::Excel excel;
    excel.create(path, true);
    excel.renameSheet("Sheet1", "点表");
    excel.writeRange(
        "点表", 1, 1,
        {{std::string("template_key"), std::string("standard_code"), std::string("vendor_name"),
          std::string("address_space"), std::string("address_offset"), std::string("access"),
          std::string("wire_type"), std::string("byte_order"), std::string("scale"),
          std::string("offset"), std::string("unit")},
         {std::string("power"), std::string("active_power"), std::string("厂家,功率"),
          std::string("holding_register"), int64_t{0}, std::string("read"), std::string("f32"),
          std::string("CDAB"), -0.001, 0.0, std::string("kW")}});
    excel.save();
    excel.close();
    auto result = import_point_table_file(path.u8string(), core::DeviceId("pcs-1"), "binding-1");
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(result.rows[0].vendor_name, "厂家,功率");
    EXPECT_EQ(result.rows[0].line, 2u);
    excel.open(path);
    excel.writeFormula("点表", "I2", "1+1");
    excel.save();
    excel.close();
    result = import_point_table_file(path.u8string(), core::DeviceId("pcs-1"), "binding-1");
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.issues[0].line, 2u);
    EXPECT_EQ(result.issues[0].column, "I2");
    std::filesystem::remove(path);
}

struct Fixture {
    model::ModelCatalog catalog;
    model::DevicePointDraft draft;

    Fixture() {
        model::PcsDeviceModel pcs;
        auto type = pcs.definition();
        model::AssetNode asset;
        asset.asset_id = core::AssetId("pcs-asset");
        asset.station_id = core::StationId("station-1");
        asset.code = "pcs01";
        asset.asset_type = model::AssetType::Device;
        catalog.assets.push_back(asset);
        draft.device_id = core::DeviceId("pcs-1");
        draft.capabilities = pcs.capabilities();
        for (const auto& definition : type.standard_points) {
            auto point = model::make_standard_point(
                definition, core::PointId("point-" + definition.semantic_code), asset.asset_id,
                definition.semantic_code);
            const core::PointId point_id = model::as_base(point).point_id;
            catalog.add_point(std::move(point));
            model::DeviceStandardPointConfig config;
            config.device_id = draft.device_id;
            config.point_id = point_id;
            config.standard_code = definition.semantic_code;
            draft.points.push_back(config);
        }
        catalog.device_types.push_back(std::move(type));
        model::Device device;
        device.device_id = draft.device_id;
        device.asset_id = asset.asset_id;
        device.type_version_id = "pcs-v1";
        catalog.devices.push_back(device);

        model::ChannelConfig channel;
        channel.channel_id = core::ChannelId("channel-1");
        channel.station_id = asset.station_id;
        channel.purpose = model::ChannelPurpose::Both;
        channel.profile.protocol = "modbus_tcp";
        catalog.channels.push_back(channel);
        model::DeviceChannelBinding binding;
        binding.binding_id = "binding-1";
        binding.device_id = device.device_id;
        binding.channel_id = channel.channel_id;
        binding.operations = {model::BindingOp::Collect, model::BindingOp::Control};
        catalog.bindings.push_back(binding);
        catalog.build_indexes();
    }
};

TEST(PointTableImport, ParsesBomQuotedNameAndExplicitAddress) {
    const std::string csv = std::string("\xEF\xBB\xBF") + kHeader +
                            "power,active_power,\"厂家,有功\",holding_register,1,read,f32,"
                            "CDAB,-0.001,0,kW\r\n";
    auto imported = parse_point_table_csv(csv, core::DeviceId("pcs-1"), "binding-1");
    ASSERT_TRUE(imported.ok());
    ASSERT_EQ(imported.rows.size(), 1u);
    EXPECT_EQ(imported.rows[0].vendor_name, "厂家,有功");
    EXPECT_EQ(imported.rows[0].address_offset, 1u);
    EXPECT_EQ(imported.rows[0].byte_order, "CDAB");
}

TEST(PointTableImport, ReportsRowAndColumnErrors) {
    auto imported = parse_point_table_csv(std::string(kHeader) +
                                              "power,active_power,有功,holding_register,40001,read,"
                                              "f32,,nan,0,kW\n",
                                          core::DeviceId("pcs-1"), "binding-1");
    ASSERT_FALSE(imported.ok());
    EXPECT_EQ(imported.issues.front().line, 2u);
    EXPECT_FALSE(imported.issues.front().column.empty());
}

TEST(PointTableImport, RequiresSelectedDeviceAndBinding) {
    auto imported = parse_point_table_csv(std::string(kHeader) + power_row(), core::DeviceId(), "");
    EXPECT_FALSE(imported.ok());
    EXPECT_EQ(imported.issues[0].column, "device_id");
}

TEST(PointTableLoad, LoadsMappedPointAndPreservesStableId) {
    Fixture fixture;
    auto imported = parse_point_table_csv(std::string(kHeader) + power_row(),
                                          fixture.draft.device_id, "binding-1");
    auto loaded =
        load_point_table(fixture.catalog, fixture.draft, imported, PointTableLoadMode::Append);
    ASSERT_TRUE(loaded.ok());
    ASSERT_EQ(loaded.catalog.vendor_routes.size(), 1u);
    EXPECT_EQ(loaded.catalog.vendor_routes[0].point_id, core::PointId("point-active_power"));
    EXPECT_EQ(loaded.catalog.vendor_routes[0].vendor_address, "modbus:holding_register:0");
    EXPECT_EQ(loaded.catalog.vendor_routes[0].scale, -0.001);
    const std::string moved_row =
        "power,active_power,厂家功率,holding_register,12,read,f32,CDAB,-0.001,0,kW\n";
    auto moved = parse_point_table_csv(std::string(kHeader) + moved_row, fixture.draft.device_id,
                                       "binding-1");
    auto again = load_point_table(loaded.catalog, loaded.draft, moved, PointTableLoadMode::Replace);
    ASSERT_TRUE(again.ok());
    ASSERT_EQ(again.catalog.vendor_routes.size(), 1u);
    EXPECT_EQ(again.catalog.vendor_routes[0].point_id, core::PointId("point-active_power"));
    EXPECT_EQ(again.catalog.vendor_routes[0].route_id, loaded.catalog.vendor_routes[0].route_id);
    EXPECT_EQ(again.catalog.vendor_routes[0].vendor_address, "modbus:holding_register:12");
    EXPECT_FALSE(
        load_point_table(loaded.catalog, loaded.draft, imported, PointTableLoadMode::Append).ok());
}

TEST(PointTableLoad, InvalidUnitDoesNotPartiallyApply) {
    Fixture fixture;
    const std::string bad_row =
        "power,active_power,厂家功率,holding_register,0,read,f32,CDAB,1,0,W\n";
    auto imported =
        parse_point_table_csv(std::string(kHeader) + bad_row, fixture.draft.device_id, "binding-1");
    auto loaded =
        load_point_table(fixture.catalog, fixture.draft, imported, PointTableLoadMode::Append);
    EXPECT_FALSE(loaded.ok());
    EXPECT_TRUE(loaded.catalog.vendor_routes.empty());
    EXPECT_EQ(loaded.draft.points[5].state, model::StandardPointState::Unconfigured);
}

TEST(PointTableLoad, RejectsModifiedImportResultAtomically) {
    Fixture fixture;
    auto imported = parse_point_table_csv(std::string(kHeader) + power_row(),
                                          fixture.draft.device_id, "binding-1");
    ASSERT_TRUE(imported.ok());
    imported.rows[0].address_offset = 70000;
    auto loaded =
        load_point_table(fixture.catalog, fixture.draft, imported, PointTableLoadMode::Append);
    EXPECT_FALSE(loaded.ok());
    EXPECT_TRUE(loaded.catalog.vendor_routes.empty());
    EXPECT_EQ(loaded.draft.points.size(), fixture.draft.points.size());
}

TEST(PointTableLoad, RejectsOtherDeviceBinding) {
    Fixture fixture;
    fixture.catalog.bindings[0].device_id = core::DeviceId("other-device");
    fixture.catalog.build_indexes();
    auto imported = parse_point_table_csv(std::string(kHeader) + power_row(),
                                          fixture.draft.device_id, "binding-1");
    auto loaded =
        load_point_table(fixture.catalog, fixture.draft, imported, PointTableLoadMode::Append);
    EXPECT_FALSE(loaded.ok());
    EXPECT_TRUE(loaded.catalog.vendor_routes.empty());
}

TEST(PointTableLoad, KeepsUnmappedVendorRowForReview) {
    Fixture fixture;
    const std::string raw = "extra,,厂商诊断点,input_register,5,read,u16,,1,0,V\n";
    auto imported =
        parse_point_table_csv(std::string(kHeader) + raw, fixture.draft.device_id, "binding-1");
    auto loaded =
        load_point_table(fixture.catalog, fixture.draft, imported, PointTableLoadMode::Append);
    ASSERT_TRUE(loaded.ok());
    EXPECT_TRUE(loaded.catalog.vendor_routes.empty());
    ASSERT_EQ(loaded.vendor_rows.size(), 1u);
    EXPECT_EQ(loaded.vendor_rows[0].template_key, "extra");
}

TEST(PointTableImport, ReadsFileWithoutChangingConfiguration) {
    const std::string path = ::testing::TempDir() + "ems_point_table_import_test.csv";
    {
        std::ofstream out(path, std::ios::binary);
        out << kHeader << power_row();
    }
    auto imported = import_point_table_file(path, core::DeviceId("pcs-1"), "binding-1");
    ASSERT_TRUE(imported.ok());
    EXPECT_EQ(imported.rows.size(), 1u);
    std::remove(path.c_str());
}

}  // namespace
}  // namespace ems::config
